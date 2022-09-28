/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "dhcpserver.h"
#include "dnsserver.h"

#define TCP_PORT 80
#define printf printf
#define POLL_TIME_S 5
#define HTTP_GET "GET"
#define HTTP_RESPONSE_HEADERS "HTTP/1.1 %d OK\nContent-Length: %d\nContent-Type: text/html; charset=utf-8\nConnection: close\n\n"

#define HTTP_RESPONSE_REDIRECT "HTTP/1.1 302 Redirect\nLocation: http://%s\n\n"

#define HTML_SOURCE "<html><head><title>Automation 2040W Takeover</title></head><body><h1>Automation 2040W Takeover</h1> \
		<p>%s</p><h2>ADC</h2><p>%s</p>\
		<h2>GPIO Inputs</h2><p>%s</p>\
		<h2>Relays</h2><p>%s</p>\
		<h2>GPIO Outputs</h2><p>%s</p></body><html>"

#define NUM_OUT_GPIOS 3
int gpios_out[] = {16,17,18};

#define NUM_IN_GPIOS 4
int gpios_in[] = {19,20,21,22};

#define NUM_ANALOGUE_GPIOS 3
int gpios_analogue[] = {26,27,28};

#define NUM_RELAY_GPIOS 3
int gpios_relay[] = {9,10,11};



typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    bool complete;
    ip_addr_t gw;
} TCP_SERVER_T;

typedef struct TCP_CONNECT_STATE_T_ {
    struct tcp_pcb *pcb;
    int sent_len;
    char headers[128];
    char result[2048];
    int header_len;
    int result_len;
    ip_addr_t *gw;
} TCP_CONNECT_STATE_T;

static err_t tcp_close_client_connection(TCP_CONNECT_STATE_T *con_state, struct tcp_pcb *client_pcb, err_t close_err) {
    if (client_pcb) {
        assert(con_state && con_state->pcb == client_pcb);
        tcp_arg(client_pcb, NULL);
        tcp_poll(client_pcb, NULL, 0);
        tcp_sent(client_pcb, NULL);
        tcp_recv(client_pcb, NULL);
        tcp_err(client_pcb, NULL);
        err_t err = tcp_close(client_pcb);
        if (err != ERR_OK) {
            printf("close failed %d, calling abort\n", err);
            tcp_abort(client_pcb);
            close_err = ERR_ABRT;
        }
        if (con_state) {
            free(con_state);
        }
    }
    return close_err;
}

static void tcp_server_close(TCP_SERVER_T *state) {
    if (state->server_pcb) {
        tcp_arg(state->server_pcb, NULL);
        tcp_close(state->server_pcb);
        state->server_pcb = NULL;
    }
}

static err_t tcp_server_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    printf("tcp_server_sent %u\n", len);
    con_state->sent_len += len;
    if (con_state->sent_len >= con_state->header_len + con_state->result_len) {
        printf("all done\n");
        return tcp_close_client_connection(con_state, pcb, ERR_OK);
    }
    return ERR_OK;
}

int gpio_server_content(TCP_CONNECT_STATE_T *con_state, const char *request, const char *params, char *result) {
		///param check GPIO=%d must be between 0 and 32 (32 being LED).
		///state = %d (i.e. 1 or 0)
		
		printf("requested page %s\n", request);
		
		printf("generating content\n");
		/// note two separate sscanf's because we don't know which order the parameters will be in
		
		printf("\nparams: %s\n", params);
		
		int gpio=-1;
		int gpio_test = sscanf(params, "GPIO=%d", &gpio);
		//what does this return? -- the number of chars updated
		printf("\n\n GPIO %d", gpio);
		printf("\ngpio test %d\n", gpio_test);
		
		int state=-1;
		
		int state_test = sscanf(strstr(params, "state="), "state=%d", &state);
		printf("scanned params\n");
		
		printf("\n\n state %d", state);
		
		printf("\nparams: %s\n", params);
		
		char update[32] = "";// create a blank string
		
		
		
		//should I also check that the GPIO is in the GPIOS_OUT?
		if(gpio_test == 1 && state_test ==1) {
			printf("\n\nchanging GPIOs\n\n");
			//there were valid parameters
			if (gpio < 33) {
				gpio_put((uint)gpio, (uint)state);
				printf("\n****setting gpio %d to %d ***\n", gpio, state);
			}
			else {
				cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, state);
			}
			int update_state = snprintf(update, 32, "<p> GPIO %d updated to %s</p>", gpio, state?"on":"off");
		}
		
		printf("states updated");
	
		
		//Can return the state of all GPIOs
		///there is also gpio_get_All, but still need to loop through it all.
		char gpio_out_lines[500] = "";
		char line[64];
		int counter = 1;
		for(int i=0;i<NUM_OUT_GPIOS;i++){ // get current GPIO values NOTE -- WORK OUT WHICH ONES are causing a problem
			if (gpio_get(gpios_out[i])) {
				snprintf(line, 64, "Output %d is on, turn <a href='/?GPIO=%d&state=0'>off</a><br>", counter, gpios_out[i]);
			}
			else {
				snprintf(line, 64,"Output %d is off, turn <a href='/?GPIO=%d&state=1'>on</a><br>", counter, gpios_out[i]);
			}
			counter++;
			
			strcat(gpio_out_lines, line);
		}
		
		char gpio_relay_lines[500] = "";
		counter =1;
		for(int i=0;i<NUM_RELAY_GPIOS;i++){ 
			if (gpio_get(gpios_relay[i])) {
				snprintf(line, 64, "Relay %d is on, turn <a href='/?GPIO=%d&state=0'>off</a><br>", counter, gpios_relay[i]);
			}
			else {
				snprintf(line, 64,"Relay %d is off, turn <a href='/?GPIO=%d&state=1'>on</a><br>", counter, gpios_relay[i]);
			}
			///how to add line to output? strcat will add it on the end
			counter++;
			strcat(gpio_relay_lines, line);
		}
		
		if (cyw43_arch_gpio_get(CYW43_WL_GPIO_LED_PIN)) {
			snprintf(line, 64, "LED is on, turn <a href='/?GPIO=40&state=0'>off</a><br>");
		}
		else{
			snprintf(line, 64, "LED is off, turn <a href='/?GPIO=40&state=1'>on</a><br>");
		}
		strcat(gpio_out_lines, line);
		
		//now do input
		
		char gpio_in_lines[500] = ""; // note -- could calculate acutal needed size, but there's enough memory
		counter =1;
		for(int i=0;i<NUM_IN_GPIOS;i++) {
			if(gpio_get(gpios_in[i])) {
				snprintf(line, 64, "Input %d is high<br>", counter);
			}
			else {
				snprintf(line, 64, "Input %d is low<br>", counter);
			}
			strcat(gpio_in_lines, line);
			
		}
		
		char gpio_analogue_lines[500] = ""; // only four available
		const float conversion_factor = 45.0f / (1 << 12);
		counter = 1;
		for(int i=0; i<NUM_ANALOGUE_GPIOS; i++) {
			if (gpios_analogue[i] == 26) {
				adc_select_input(0);
			}
			if (gpios_analogue[i] == 27) {
				adc_select_input(1);
			}
			if (gpios_analogue[i] == 28) {
				adc_select_input(2);
			}
			if (gpios_analogue[i] == 29) {
				adc_select_input(3);
			}
			
			uint16_t result = adc_read();
			
			snprintf(line, 64, "ADC %d has an analogue voltage of %f<br>", counter, result*conversion_factor);			
			strcat(gpio_analogue_lines, line);
			counter++;
			
		}
		
		
		char html_string[2000] = ""; // might be massively too much
		
		snprintf(html_string, 2000, HTML_SOURCE, update, gpio_analogue_lines, gpio_in_lines, gpio_relay_lines, gpio_out_lines);
		
		//super inefficient, but just testing it out.
		int len = snprintf(con_state->result, sizeof(con_state->result), html_string);
		
		//printf("returning len:");
		printf("about to return \n");
		return len;
}

err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    if (!p) {
        printf("connection closed\n");
        return tcp_close_client_connection(con_state, pcb, ERR_OK);
    }
    assert(con_state && con_state->pcb == pcb);
    if (p->tot_len > 0) {
        printf("tcp_server_recv %d err %d\n", p->tot_len, err);
#if 0
        for (struct pbuf *q = p; q != NULL; q = q->next) {
            printf("in: %.*s\n", q->len, q->payload);
        }
#endif
        // Copy the request into the buffer
        pbuf_copy_partial(p, con_state->headers, p->tot_len > sizeof(con_state->headers) - 1 ? sizeof(con_state->headers) - 1 : p->tot_len, 0);

        // Handle GET request
        if (strncmp(HTTP_GET, con_state->headers, sizeof(HTTP_GET) - 1) == 0) {
            char *request = con_state->headers + sizeof(HTTP_GET); // + space
            char *params = strchr(request, '?');
            if (params) {
                if (*params) {
                    char *space = strchr(request, ' ');
                    *params++ = 0; // terminates the request string, then ++'s the value of the params pointer to be the next char. Hence this creates two strings requests and params
                    if (space) {
                        *space = 0; // terminates string at a space
                    }
                } else {
                    params = NULL;
                }
            }

            // Generate content
			printf("calling gpio_server_content\n");
			int res_len = gpio_server_content(con_state, request, params, con_state->result);

			stdio_flush();
			sleep_ms(1000);
			printf("Request: %s?%s\n", request, params);
			
			con_state->result_len = res_len;
			
            printf("Result: %d\n", con_state->result_len);
	sleep_ms(1000);
			
            // Check we had enough buffer space
            if (con_state->result_len > sizeof(con_state->result) - 1) {
                printf("Too much result data %d\n", con_state->result_len);
                return tcp_close_client_connection(con_state, pcb, ERR_CLSD);
            }

            // Generate web page
            if (con_state->result_len > 0) {
                con_state->header_len = snprintf(con_state->headers, sizeof(con_state->headers), HTTP_RESPONSE_HEADERS,
                    200, con_state->result_len);
                if (con_state->header_len > sizeof(con_state->headers) - 1) {
                    printf("Too much header data %d\n", con_state->header_len);
                    return tcp_close_client_connection(con_state, pcb, ERR_CLSD);
                }
            } else {
                // Send redirect
                con_state->header_len = snprintf(con_state->headers, sizeof(con_state->headers), HTTP_RESPONSE_REDIRECT,
                    ipaddr_ntoa(con_state->gw));
                printf("Sending redirect %s", con_state->headers);
            }

            // Send the headers to the client
            con_state->sent_len = 0;
            err_t err = tcp_write(pcb, con_state->headers, con_state->header_len, 0);
            if (err != ERR_OK) {
                printf("failed to write header data %d\n", err);
                return tcp_close_client_connection(con_state, pcb, err);
            }

            // Send the body to the client
            if (con_state->result_len) {
                err = tcp_write(pcb, con_state->result, con_state->result_len, 0);
                if (err != ERR_OK) {
                    printf("failed to write result data %d\n", err);
                    return tcp_close_client_connection(con_state, pcb, err);
                }
            }
        }
        tcp_recved(pcb, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static err_t tcp_server_poll(void *arg, struct tcp_pcb *pcb) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    printf("tcp_server_poll_fn\n");
    return tcp_close_client_connection(con_state, pcb, ERR_OK); // Just disconnect clent?
}

static void tcp_server_err(void *arg, err_t err) {
    TCP_CONNECT_STATE_T *con_state = (TCP_CONNECT_STATE_T*)arg;
    if (err != ERR_ABRT) {
        printf("tcp_client_err_fn %d\n", err);
        tcp_close_client_connection(con_state, con_state->pcb, err);
    }
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if (err != ERR_OK || client_pcb == NULL) {
        printf("failure in accept\n");
        return ERR_VAL;
    }
    printf("client connected\n");

    // Create the state for the connection
    TCP_CONNECT_STATE_T *con_state = calloc(1, sizeof(TCP_CONNECT_STATE_T));
    if (!con_state) {
        printf("failed to allocate connect state\n");
        return ERR_MEM;
    }
    con_state->pcb = client_pcb; // for checking
    con_state->gw = &state->gw;

    // setup connection to client
    tcp_arg(client_pcb, con_state);
    tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_poll(client_pcb, tcp_server_poll, POLL_TIME_S * 2);
    tcp_err(client_pcb, tcp_server_err);

    return ERR_OK;
}

static bool tcp_server_open(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    printf("starting server on port %u\n", TCP_PORT);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        printf("failed to create pcb\n");
        return false;
    }

    err_t err = tcp_bind(pcb, NULL, TCP_PORT);
    if (err) {
        printf("failed to bind to port %d\n");
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        printf("failed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    return true;
}

int main() {
	
    stdio_init_all();
	
    sleep_ms(10000);
	
	TCP_SERVER_T *state = calloc(1, sizeof(TCP_SERVER_T));
    if (!state) {
        printf("failed to allocate state\n");
        return 1;
    }
	
	if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }

#ifdef WIFI_SSID
    printf("Connecting to WiFi...\n");
	 cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    } else {
        printf("Connected.\n");
    }
	
	printf("Starting server at %s \n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

#else

    const char *ap_name = "picow_test";
    const char *password = "password";

    cyw43_arch_enable_ap_mode(ap_name, password, CYW43_AUTH_WPA2_AES_PSK);

    ip4_addr_t mask;
    IP4_ADDR(&state->gw, 192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);

    // Start the dhcp server
    dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &state->gw, &mask);

    // Start the dns server
    dns_server_t dns_server;
    dns_server_init(&dns_server, &state->gw);
#endif

    if (!tcp_server_open(state)) {
        printf("failed to open server\n");
        return 1;
    }
	
	for (int i=0;i<NUM_OUT_GPIOS;i++) {
		gpio_init(gpios_out[i]);
		gpio_set_dir(gpios_out[i], GPIO_OUT);
		gpio_put(gpios_out[i],0); // probably safest to start with all IOs off
	}
	
	for (int i=0;i<NUM_RELAY_GPIOS;i++) {
		gpio_init(gpios_relay[i]);
		gpio_set_dir(gpios_relay[i], GPIO_OUT);
		gpio_put(gpios_relay[i],0); // probably safest to start with relays off.
	}
	
	
	for (int i=0;i<NUM_IN_GPIOS;i++) {
		gpio_init(gpios_in[i]);
		gpio_set_dir(gpios_in[i], GPIO_IN);
	}
	
	adc_init();
	for (int i=0;i<NUM_ANALOGUE_GPIOS;i++) {
		adc_gpio_init(gpios_analogue[i]);
	}

    while(!state->complete) {
        // the following #ifdef is only here so this same example can be used in multiple modes;
        // you do not need it in your code
#if PICO_CYW43_ARCH_POLL
        // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
        // main loop (not from a timer) to check for WiFi driver or lwIP work that needs to be done.
        cyw43_arch_poll();
        sleep_ms(1);
#else
        // if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
        // is done via interrupt in the background. This sleep is just an example of some (blocking)
        // work you might be doing.
        sleep_ms(1000);
#endif

	// check button state here
    }
	
#ifndef WIFI_SSID
    dns_server_deinit(&dns_server);
    dhcp_server_deinit(&dhcp_server);
#endif
    cyw43_arch_deinit();
    return 0;
}
