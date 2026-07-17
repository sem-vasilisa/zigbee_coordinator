#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zboss_api.h>
#include <zb_mem_config_max.h>          /* konfiguracja pamięci ZBOSS — WYMAGANE */
#include <zigbee/zigbee_error_handler.h>
#include <zigbee/zigbee_app_utils.h>    /* zigbee_enable(), default handler */
#include <zb_nrf_platform.h>
#include "zb_range_extender.h"

#include <zephyr/logging/log.h>

// LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);   /* obok pozostałych #include */

LOG_MODULE_REGISTER(zigbee_coordinator, LOG_LEVEL_INF);

#define LED_NODE DT_ALIAS(led0)
#define COORD_EP  10   /* endpoint On/Off clienta, taki sam jak ustawiliśmy w pierwszym kroku  */

/* toggle i przycisk*/
#define SW_NODE DT_ALIAS(sw0)             /* SW1 na donglu */
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW_NODE, gpios);
static struct gpio_callback button_cb;

/* here we will store end-device information */
static struct {
    bool           used, bound; /* used = have we seen a device yet, bound = did binding succeed */
    zb_ieee_addr_t ieee;
    zb_uint16_t    short_addr;
    zb_uint8_t     remote_ep;   /* endpoint number on the remote device */
} dev;

/* --- lista endpointów odkrywanego urządzenia --- */
static struct { zb_uint8_t eps[16], count, idx; } disc; /* eps is a list of enpoint numbers the bulb reported, count how many were returned and idx which one we're currently checking*/

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

/* Minimalny kontekst urządzenia ZCL: 1 endpoint (EP 10), Basic + Identify.
 * ZBOSS wymaga zarejestrowanego kontekstu PRZED zigbee_enable(), inaczej crash. */
struct zb_device_ctx {
    zb_zcl_basic_attrs_t    basic_attr;
    zb_zcl_identify_attrs_t identify_attr;
};
static struct zb_device_ctx dev_ctx;

static void send_simple_desc_req(zb_bufid_t bufid);

/* if binding succeeds */
static void bind_cb(zb_bufid_t bufid)
{
    zb_zdo_bind_resp_t *r = (zb_zdo_bind_resp_t *)zb_buf_begin(bufid);
    if (r->status == ZB_ZDP_STATUS_SUCCESS) {
        dev.bound = true;
        dev.remote_ep = disc.eps[disc.idx - 1];
        LOG_INF("Bind OK: On/Off → EP %d", dev.remote_ep);
    } else {
        LOG_ERR("Bind failed: %d", r->status);
    }
    zb_buf_free(bufid);
}
/* binding*/
static void do_bind(zb_bufid_t bufid)
{
    zb_zdo_bind_req_param_t *req = ZB_BUF_GET_PARAM(bufid, zb_zdo_bind_req_param_t); /* get a pointer to the bind request parameters */
    zb_ieee_addr_t my_ieee;
    zb_get_long_address(my_ieee);
    ZB_MEMCPY(req->src_address, my_ieee, sizeof(zb_ieee_addr_t));
    req->src_endp      = COORD_EP; /* source endpoint */
    req->cluster_id    = ZB_ZCL_CLUSTER_ID_ON_OFF; /* which cluster this binding is for*/
    req->dst_addr_mode = ZB_BIND_DST_ADDR_MODE_64_BIT_EXTENDED; /* destination address mode */
    ZB_MEMCPY(&req->dst_address.addr_long, dev.ieee, sizeof(zb_ieee_addr_t));
    req->dst_endp     = disc.eps[disc.idx - 1];
    req->req_dst_addr = zb_get_short_address();   /* where the binding request is being sent */
    zb_zdo_bind_req(bufid, bind_cb);
}

/* Krok 2: sprawdź, czy na tym EP jest On/Off server */
static void simple_desc_cb(zb_bufid_t bufid)
{
    zb_zdo_simple_desc_resp_t *r = (zb_zdo_simple_desc_resp_t *)zb_buf_begin(bufid);
    bool found = false;
    if (r->hdr.status == ZB_ZDP_STATUS_SUCCESS) {
        /* loop over the list of clusters looking for a on-off cluster */
        for (zb_uint8_t i = 0; i < r->simple_desc.app_input_cluster_count; i++)
            if (r->simple_desc.app_cluster_list[i] == ZB_ZCL_CLUSTER_ID_ON_OFF)
                found = true;
    }
    zb_buf_free(bufid); /* free the buffer */

    if (found)                       zb_buf_get_out_delayed(do_bind); /* create the binding*/
    else if (disc.idx < disc.count)  zb_buf_get_out_delayed(send_simple_desc_req); /* continue to the next endpoint */
    else                             LOG_WRN("No On/Off server found");
}

static void send_simple_desc_req(zb_bufid_t bufid)
{
    zb_zdo_simple_desc_req_t *req = (zb_zdo_simple_desc_req_t *)
        zb_buf_initial_alloc(bufid, sizeof(zb_zdo_simple_desc_req_t)); /* reseres place in the buffer for the request */
    req->nwk_addr = dev.short_addr; /* fill in which device are we asking, by short address */
    req->endpoint = disc.eps[disc.idx++]; /* fill in the endpoint we are asking about */
    zb_zdo_simple_desc_req(bufid, simple_desc_cb); /* send the request, when the response comes back, call simple_desc_cb */
}

/* gets the list of endpoints and saves it */
static void active_ep_cb(zb_bufid_t bufid)
{
    zb_zdo_ep_resp_t *r = (zb_zdo_ep_resp_t *)zb_buf_begin(bufid); // gets from the buffer reads header
    zb_uint8_t *list = (zb_uint8_t *)(r + 1); // after the header, gets the list of endpoints that were returned
    disc.count = MIN(r->ep_count, ARRAY_SIZE(disc.eps)); // how many endpoints were returned, but not more than the size of our array
    disc.idx = 0;
    for (zb_uint8_t i = 0; i < disc.count; i++) disc.eps[i] = list[i]; // copy each enpoint to our disc.eps array, to our memory
    zb_buf_free(bufid); // return buffer memory to the zboss
    if (disc.count) zb_buf_get_out_delayed(send_simple_desc_req); // if we have an endpoint, request a new buffer and call simple description to find a cluster
}

/* what endpoints do we have */
static void send_active_ep_req(zb_bufid_t bufid)
{
    zb_zdo_active_ep_req_t *req = (zb_zdo_active_ep_req_t *)
        zb_buf_initial_alloc(bufid, sizeof(zb_zdo_active_ep_req_t)); /*bufid is a buffer we had before, gets the space inside that buffer for an active_ep_req, write it ino req*/
    req->nwk_addr = dev.short_addr; /* fill in which device are we asking, by short address*/
    zb_zdo_active_ep_req(bufid, active_ep_cb); /* send the request, when the response comes back, call active_ep_cb */
}

static void send_toggle_cmd(zb_bufid_t bufid)   /* już w wątku ZBOSS */
{
    if (!dev.bound) { zb_buf_free(bufid); return; }
    LOG_INF("Sending Toggle to 0x%04x EP %d", dev.short_addr, dev.remote_ep);
    ZB_ZCL_ON_OFF_SEND_TOGGLE_REQ(bufid, dev.short_addr,
        ZB_APS_ADDR_MODE_16_ENDP_PRESENT,   /* adresowanie jawne: short + EP */
        dev.remote_ep, COORD_EP,
        ZB_AF_HA_PROFILE_ID, ZB_ZCL_DISABLE_DEFAULT_RESPONSE, NULL);
}

static void button_pressed(const struct device *port,
                           struct gpio_callback *cb, uint32_t pins)
{
    /* To jest ISR — NIE wolno tu wołać API ZBOSS bezpośrednio.
     * Delegujemy do wątku ZBOSS przez pobranie bufora. */
    zb_buf_get_out_delayed(send_toggle_cmd);
}


ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(identify_attr_list,
    &dev_ctx.identify_attr.identify_time);
ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST(basic_attr_list,
    &dev_ctx.basic_attr.zcl_version, &dev_ctx.basic_attr.power_source);

ZB_DECLARE_SIMPLE_DESC(2, 1); 
  /* raz, z literałami: 2 clustery IN + 1 OUT */

ZB_DECLARE_RANGE_EXTENDER_CLUSTER_LIST(coord_ep1_clusters,
    basic_attr_list, identify_attr_list);

ZB_DECLARE_RANGE_EXTENDER_EP(coord_ep1, 10, coord_ep1_clusters);
ZBOSS_DECLARE_DEVICE_CTX_1_EP(coordinator_ctx, coord_ep1);

static void app_clusters_attr_init(void)
{
    dev_ctx.basic_attr.zcl_version  = ZB_ZCL_VERSION;
    dev_ctx.basic_attr.power_source = ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE;
    dev_ctx.identify_attr.identify_time =
        ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;
}

static void handle_device_joined(zb_uint16_t short_addr, const zb_ieee_addr_t ieee)
{
    // LOG_INF("=====================================================");
    // LOG_INF("New device joined: 0x%04x", short_addr);
    // LOG_INF("  IEEE: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
    //     ieee[7], ieee[6], ieee[5], ieee[4],
    //     ieee[3], ieee[2], ieee[1], ieee[0]);
    // LOG_INF("=====================================================");

    if (dev.used) return;                 /* obsługujemy jedno urządzenie */
    dev.used = true;
    dev.bound = false;
    dev.short_addr = short_addr;
    ZB_MEMCPY(dev.ieee, ieee, sizeof(zb_ieee_addr_t));
    LOG_INF("New device 0x%04x — starting discovery", short_addr);
    zb_buf_get_out_delayed(send_active_ep_req);

}


static volatile bool network_up = false; // is network ready

void zboss_signal_handler(zb_bufid_t bufid)
{
    // get details about the event 
    zb_zdo_app_signal_hdr_t  *sg_p = NULL;
    zb_zdo_app_signal_type_t  sig  = zb_get_app_signal(bufid, &sg_p); // reads from the buffer which event has occurred
    zb_ret_t                  status = ZB_GET_APP_SIGNAL_STATUS(bufid); // did it succeed or fail
    switch (sig) {
        case ZB_BDB_SIGNAL_DEVICE_FIRST_START:      /* świeży NVRAM → utwórz sieć */
            if (status == RET_OK){
                LOG_INF("First start — forming new network");
                bdb_start_top_level_commissioning(ZB_BDB_NETWORK_FORMATION);
            }
            break;

        case ZB_BDB_SIGNAL_FORMATION:               /* sieć utworzona → otwórz na join */
            if (status == RET_OK)
            {
                LOG_INF("Network formed — starting steering");
                bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);
            }
            break;

        case ZB_BDB_SIGNAL_DEVICE_REBOOT:           /* sieć odtworzona z NVRAM */
            if (status == RET_OK){
                LOG_INF("Device reboot — starting steering");
                bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);
            }
            break;

        case ZB_BDB_SIGNAL_STEERING:                /* permit join aktywny (180 s) */
            if (status == RET_OK){
                network_up = true;

                zb_bdb_set_legacy_device_support(1);   /* dopuść starsze urządzenia */

                LOG_INF("Network steering started");
                LOG_INF("PAN ID: 0x%04x, channel: %d",
                zb_get_pan_id(), zb_get_current_channel());
            }                  /* ← dioda to pokaże */
            break;

        case ZB_ZDO_SIGNAL_DEVICE_ANNCE: {
            zb_zdo_signal_device_annce_params_t *a =
                ZB_ZDO_SIGNAL_GET_PARAMS(sg_p, zb_zdo_signal_device_annce_params_t); // treat raw data sg_p as a deice annce
            handle_device_joined(a->device_short_addr, a->ieee_addr);// gets the short and long address and calls the function to handle it
        } break;

        case ZB_ZDO_SIGNAL_DEVICE_AUTHORIZED: {
            zb_zdo_signal_device_authorized_params_t *auth =
                ZB_ZDO_SIGNAL_GET_PARAMS(sg_p, zb_zdo_signal_device_authorized_params_t); // extracts the authorization parameters from the signal
            // normal login from new zigbee success || older device login success, puts it into a normal state and calls the function to handle it
            if (auth->authorization_status == ZB_ZDO_TCLK_AUTHORIZATION_SUCCESS ||
                auth->authorization_status == ZB_ZDO_LEGACY_DEVICE_AUTHORIZATION_SUCCESS) {
                handle_device_joined(auth->short_addr, auth->long_addr);
            }
        } break;

        default:
            ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));
            break;
    }

    if (bufid) zb_buf_free(bufid); // if buffer not empty - clean
}

int main(void)
{
    LOG_INF("Starting Zigbee Coordinator");
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

    /* konfiguracja przycisku*/
    gpio_pin_configure_dt(&button, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb);

    ZB_AF_REGISTER_DEVICE_CTX(&coordinator_ctx);
    app_clusters_attr_init(); // starting values from the device context
    zigbee_enable();

    while (1) {
        if (network_up) {
            gpio_pin_set_dt(&led, 1);           /* sieć gotowa → światło ciągłe */
            k_sleep(K_MSEC(1000));
        } else {
            gpio_pin_toggle_dt(&led);           /* czekamy → szybkie miganie */
            k_sleep(K_MSEC(150));
        }
    }
    return 0;
}