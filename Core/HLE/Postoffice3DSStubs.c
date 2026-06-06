#include <stddef.h>

#include "ext/aemu_postoffice/client/postoffice_client.h"

static void postoffice3ds_fail(int *state) {
	if (state) {
		*state = AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
	}
}

int aemu_post_office_init() {
	return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
}

void *pdp_create_v6(const struct aemu_post_office_sock6_addr *addr, const char *pdp_mac, int pdp_port, int *state) {
	postoffice3ds_fail(state);
	return NULL;
}

void *pdp_create_v4(const struct aemu_post_office_sock_addr *addr, const char *pdp_mac, int pdp_port, int *state) {
	postoffice3ds_fail(state);
	return NULL;
}

void pdp_delete(void *pdp_handle) {
}

int pdp_send(void *pdp_handle, const char *pdp_mac, int pdp_port, const char *buf, int len, bool non_block) {
	return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
}

int pdp_recv(void *pdp_handle, char *pdp_mac, int *pdp_port, char *buf, int *len, bool non_block) {
	return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
}

int pdp_peek_next_size(void *pdp_handle) {
	return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
}

void *ptp_listen_v6(const struct aemu_post_office_sock6_addr *addr, const char *ptp_mac, int ptp_port, int *state) {
	postoffice3ds_fail(state);
	return NULL;
}

void *ptp_listen_v4(const struct aemu_post_office_sock_addr *addr, const char *ptp_mac, int ptp_port, int *state) {
	postoffice3ds_fail(state);
	return NULL;
}

void *ptp_accept(void *ptp_listen_handle, char *ptp_mac, int *ptp_port, bool nonblock, int *state) {
	postoffice3ds_fail(state);
	return NULL;
}

void *ptp_connect_v6(const struct aemu_post_office_sock6_addr *addr, const char *src_ptp_mac, int ptp_sport, const char *dst_ptp_mac, int ptp_dport, int *state) {
	postoffice3ds_fail(state);
	return NULL;
}

void *ptp_connect_v4(const struct aemu_post_office_sock_addr *addr, const char *src_ptp_mac, int ptp_sport, const char *dst_ptp_mac, int ptp_dport, int *state) {
	postoffice3ds_fail(state);
	return NULL;
}

int ptp_send(void *ptp_handle, const char *buf, int len, bool non_block) {
	return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
}

int ptp_recv(void *ptp_handle, char *buf, int *len, bool non_block) {
	return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
}

void ptp_close(void *ptp_handle) {
}

void ptp_listen_close(void *ptp_listen_handle) {
}

int ptp_peek_next_size(void *ptp_handle) {
	return AEMU_POSTOFFICE_CLIENT_SESSION_DEAD;
}

int pdp_get_native_sock(void *pdp_handle) {
	return -1;
}

int ptp_get_native_sock(void *ptp_handle) {
	return -1;
}

int ptp_listen_get_native_sock(void *ptp_listen_handle) {
	return -1;
}
