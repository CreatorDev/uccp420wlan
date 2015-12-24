/*HEADER**********************************************************************
******************************************************************************
***
*** Copyright (c) 2011, 2012, 2013, 2014 Imagination Technologies Ltd.
*** All rights reserved
***
*** This program is free software; you can redistribute it and/or
*** modify it under the terms of the GNU General Public License
*** as published by the Free Software Foundation; either version 2
*** of the License, or (at your option) any later version.
***
*** This program is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*** GNU General Public License for more details.
***
*** You should have received a copy of the GNU General Public License
*** along with this program; if not, write to the Free Software
*** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
*** USA.
***
*** File Name  : lmac_if.h
***
*** File Description:
*** This file contains the helper functions exported by LMAC interface module
*** for sending commands and receiving events from the LMAC
*****************************************************************************
*END**************************************************************************/

#ifndef _UCCP420WLAN_UMAC_IF_H_
#define _UCCP420WLAN_UMAC_IF_H_
#include <linux/skbuff.h>
#include "hal.h"
#include "host_umac_if.h"

#define UMAC_ROC_AC WLAN_AC_VO

struct umac_key {
	unsigned char *peer_mac;
	unsigned char *tx_mic;
	unsigned char *rx_mic;
	unsigned char *key;
};

struct ssid_desc {
	unsigned char ssid[MAX_SSID_LEN];
	unsigned char ssid_len;
};

struct scan_req {
	unsigned int n_channels;
	int n_ssids;
	unsigned int ie_len;
	unsigned char ie[256];
	unsigned int p2p_probe;
	/*TODO: Make this a structure*/
	unsigned short center_freq[50];
	unsigned char freq_max_power[50];
	unsigned char chan_flags[50];
	struct ssid_desc ssids[MAX_NUM_SSIDS];
};

struct peer_sta_info {
	unsigned int ht_cap;
	unsigned int ht_supported;
	unsigned int ampdu_factor;
	unsigned int ampdu_density;
	unsigned int vht_cap;
	unsigned int vht_supported;
	unsigned int rx_highest;
	unsigned int tx_params;
	unsigned int supp_rates[STA_NUM_BANDS];
	unsigned char addr[ETH_ALEN];
	unsigned char rx_mask[HT_MCS_MASK_LEN];
	unsigned char uapsd_queues;
};

/*commands*/
extern int uccp420wlan_scan(int index,
			    struct scan_req *req);

extern int uccp420wlan_scan_abort(int index);

extern int uccp420wlan_proc_tx(void);

extern int uccp420wlan_prog_tx(unsigned int queue,
			       unsigned int more_data,
#ifdef MULTI_CHAN_SUPPORT
			       int curr_chanctx_idx,
#endif
			       unsigned int tokenid,
			       bool retry);

extern int uccp420wlan_sta_add(int index,
			       struct peer_sta_info *sta);

extern int uccp420wlan_sta_remove(int index,
				  struct peer_sta_info *sta);

extern int uccp420wlan_set_rate(int rate,
				int mcs);

extern int uccp420wlan_prog_reset(unsigned int reset_type,
				  unsigned int lmac_mode);

extern int uccp420wlan_prog_vif_ctrl(int index,
				     unsigned char *vif_addr,
				     unsigned int  vif_type,
				     unsigned int  add_vif);

extern int uccp420wlan_prog_vif_basic_rates(int index,
					    unsigned char *vif_addr,
					    unsigned int basic_rate_set);

extern int uccp420wlan_prog_vif_short_slot(int index,
					   unsigned char *vif_addr,
					   unsigned int use_short_slot);

extern int uccp420wlan_prog_vif_atim_window(int index,
					    unsigned char *vif_addr,
					    unsigned int atim_window);

extern int uccp420wlan_prog_vif_aid(int index,
				    unsigned char *vif_addr,
				    unsigned int aid);

extern int uccp420wlan_prog_vif_op_channel(int index,
					   unsigned char *vif_addr,
					   unsigned char op_channel);

extern int uccp420wlan_prog_vif_conn_state(int index,
					      unsigned char *vif_addr,
					      unsigned int state);

extern int uccp420wlan_prog_vif_assoc_cap(int index,
					  unsigned char *vif_addr,
					  unsigned int caps);

extern int uccp420wlan_prog_vif_beacon_int(int index,
					   unsigned char *vif_addr,
					   unsigned int bcn_int);

extern int uccp420wlan_prog_vif_dtim_period(int index,
					    unsigned char *vif_addr,
					    unsigned int dtim_period);

extern int uccp420wlan_prog_vif_apsd_type(int index,
					  unsigned char *vif_addr,
					  unsigned int uapsd_type);

extern int uccp420wlan_prog_long_retry(int index,
				       unsigned char *vif_addr,
				       unsigned int long_retry);

extern int uccp420wlan_prog_short_retry(int index,
					unsigned char *vif_addr,
					unsigned int short_retry);

extern int uccp420wlan_prog_vif_bssid(int index,
				      unsigned char *vif_addr,
				      unsigned char *bssid);

extern int uccp420wlan_prog_vif_smps(int index,
				     unsigned char *vif_addr,
				     unsigned char smps_mode);

extern int uccp420wlan_prog_ps_state(int index,
				     unsigned char *vif_addr,
				     unsigned int powersave_state);

extern int uccp420wlan_prog_global_cfg(unsigned int rx_msdu_lifetime,
				       unsigned int tx_msdu_lifetime,
				       unsigned int sensitivity,
				       unsigned int dyn_ed_enabled,
				       unsigned char *rf_params);

extern int uccp420wlan_prog_txpower(unsigned int txpower);

extern int uccp420wlan_prog_btinfo(unsigned int bt_state);

extern int uccp420wlan_prog_mcast_addr_cfg(unsigned char  *mcast_addr,
					   unsigned int add_filter);

extern int uccp420wlan_prog_mcast_filter_control(unsigned int
						 enable_mcast_filtering);

extern int uccp420wlan_prog_rcv_bcn_mode(unsigned int  bcn_rcv_mode);
extern int uccp420wlan_prog_aux_adc_chain(unsigned int chain_id);
extern int uccp420wlan_cont_tx(int val);
extern int uccp420wlan_prog_txq_params(int index,
				       unsigned char *vif_addr,
				       unsigned int queue,
				       unsigned int aifs,
				       unsigned int txop,
				       unsigned int cwmin,
				       unsigned int cwmax,
				       unsigned int uapsd);

extern int uccp420wlan_prog_channel(unsigned int prim_ch,
				    unsigned int center_freq1,
				    unsigned int center_freq2,
				    unsigned int ch_width,
#ifdef MULTI_CHAN_SUPPORT
				    unsigned int vif_index,
#endif
				    unsigned int freq_band);

extern int uccp420wlan_prog_peer_key(int index,
				     unsigned char *vif_addr,
				     unsigned int op,
				     unsigned int key_id,
				     unsigned int key_type,
				     unsigned int cipher_type,
				     struct umac_key *key);

extern int uccp420wlan_prog_if_key(int   index,
				   unsigned char *vif_addr,
				   unsigned int op,
				   unsigned int key_id,
				   unsigned int cipher_type,
				   struct umac_key *key);

extern int uccp420wlan_prog_mib_stats(void);

extern int uccp420wlan_prog_clear_stats(void);

extern int uccp420wlan_prog_phy_stats(void);

extern int uccp420wlan_prog_ba_session_data(unsigned int op,
					    unsigned short tid,
					    unsigned short *ssn,
					    unsigned short ba_policy,
					    unsigned char *sta_addr,
					    unsigned char *peer_add);

extern int uccp420wlan_prog_vht_bform(unsigned int vht_beamform_status,
					 unsigned int vht_beamform_period);

extern int uccp420wlan_prog_roc(unsigned int roc_status,
				unsigned int roc_channel,
				unsigned int roc_duration,
				unsigned int roc_type);

#ifdef CONFIG_PM
extern int uccp420wlan_prog_econ_ps_state(int if_index,
					  unsigned int ps_state);
#endif

/* Events  */
extern void uccp420wlan_scan_complete(void *context,
				      struct host_event_scanres *scan_res,
				      unsigned char *skb,
				      unsigned int len);

extern void uccp420wlan_reset_complete(char *lmac_version,
				       void *context);

extern void uccp420wlan_rf_calib_data(struct umac_event_rf_calib_data *rf_data,
				      void *context);

extern void uccp420wlan_proc_tx_complete(struct umac_event_tx_done *txdone,
				    void *context);

extern void uccp420wlan_tx_complete(struct umac_event_tx_done *txdone,
#ifdef MULTI_CHAN_SUPPORT
				    int curr_chanctx_idx,
#endif
				    void *context);

extern void uccp420wlan_rx_frame(struct sk_buff *skb,
				 void *context);

extern void uccp420wlan_mib_stats(struct umac_event_mib_stats *mib_stats,
				  void *context);

extern void uccp420wlan_mac_stats(struct umac_event_mac_stats *mac_stats,
				  void *context);

extern void uccp420wlan_noa_event(int event,
				  struct umac_event_noa *noa_event,
				  void *context,
				  struct sk_buff *skb);

extern void uccp420wlan_ch_prog_complete(int event,
					 struct umac_event_ch_prog_complete *ch,
					 void *context);

/* Init/Deinit */

extern int uccp420wlan_lmac_if_init(void *context,
				    const char *name);

extern void uccp420wlan_lmac_if_deinit(void);

extern void uccp420_lmac_if_free_outstnding(void);

#ifdef MULTI_CHAN_SUPPORT
extern int uccp420wlan_prog_chanctx_time_info(void);
#endif

#endif /* _UCCP420WLAN_UMAC_IF_H_ */

/* EOF */

