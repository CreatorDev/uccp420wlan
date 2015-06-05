/*
 * File Name  : core.h
 *
 * This file contains the declarations of structures that will
 * be used by core, tx and rx code
 *
 * Copyright (c) 2011, 2012, 2013, 2014 Imagination Technologies Ltd.
 * All rights reserved
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#ifndef _UCCP420WLAN_CORE_H_
#define _UCCP420WLAN_CORE_H_

#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/wireless.h>
#include <linux/sched.h>
#include <linux/jiffies.h>

#include <linux/interrupt.h>
#include <net/mac80211.h>

#include <linux/dma-mapping.h>
#include <linux/atomic.h>
#include <linux/etherdevice.h>

#include "umac_if.h"
#include "descriptors.h"

extern unsigned int vht_support;
extern struct cmd_send_recv_cnt cmd_info;

extern unsigned int system_rev;

#ifdef PERF_PROFILING
extern unsigned long irq_timestamp[20];
extern unsigned int irq_ts_index;
extern spinlock_t timing_lock;
#endif

extern struct platform_driver img_uccp_driver;
extern unsigned char vif_macs[2][ETH_ALEN];

extern spinlock_t tsf_lock;

#ifdef DRIVER_DEBUG
#define DEBUG_LOG(fmt, args...) pr_debug(fmt, ##args)
#else
#define DEBUG_LOG(...) do { } while (0)
#endif

#define MAX_OUTSTANDING_CTRL_REQ 2
#define RESET_TIMEOUT 5000   /* In milli-seconds*/
#define RESET_TIMEOUT_TICKS msecs_to_jiffies(RESET_TIMEOUT)
/*100: For ROC, 500: For initial*/
#define CH_PROG_TIMEOUT 500   /* In milli-seconds*/
#define CH_PROG_TIMEOUT_TICKS msecs_to_jiffies(CH_PROG_TIMEOUT)

#ifdef CONFIG_PM
#define PS_ECON_CFG_TIMEOUT 1000
#define PS_ECON_CFG_TIMEOUT_TICKS msecs_to_jiffies(PS_ECON_CFG_TIMEOUT)
#endif

#define TX_COMPLETE_TIMEOUT 1000  /* In milli-seconds*/
#define TX_COMPLETE_TIMEOUT_TICKS msecs_to_jiffies(TX_COMPLETE_TIMEOUT)
#define SCAN_ABORT_TIMEOUT 1000
#define SCAN_ABORT_TIMEOUT_TICKS msecs_to_jiffies(SCAN_ABORT_TIMEOUT)


#define MAX_VIFS 2
#define DEFAULT_TX_ANT_SELECT 3 /* bitmap of antennas for tx, 3=> both first and
				 * second antenna to be used
				 */
#define DEFAULT_TX_POWER 15
#define DEFAULT_RTS_THRESHOLD 2347
#define SUPPORTED_FILTERS (FIF_ALLMULTI | FIF_BCN_PRBRESP_PROMISC)
#define TX_DESC_BUCKET_BOUND 32

#define MAX_DATA_SIZE (0) /* Defined in HAL (or) can be configured from proc */
#define MAX_TX_QUEUE_LEN 192
#define MAX_AUX_ADC_SAMPLES 10

#define MAX_TX_STREAMS 2 /* Maximum number of Tx streams supported */
#define MAX_RX_STREAMS 2 /* Maximum number of RX streams supported */

#define   MAX_RSSI_SAMPLES 10

#define CLOCK_MASK 0x3FFFFFFF
#define TICK_NUMRATOR 1000 /* 1 MHz */
#define TICK_DENOMINATOR 12288 /* 12288000 Hz */


enum noa_triggers {
	FROM_TX = 0,
	FROM_TX_DONE,
	FROM_EVENT_NOA
};

enum uccp420_hw_scan_status {
	HW_SCAN_STATUS_NONE,
	HW_SCAN_STATUS_PROGRESS
};

struct wifi_sync {
	unsigned int  status;
	unsigned char ts1[8];
	unsigned long long atu;
	unsigned char  bssid[8];
	unsigned char  name[10];
	unsigned int  ts2;
};

struct wifi_params {
	int ed_sensitivity;
	int num_vifs;
	int tx_fixed_rate;
	int tx_fixed_mcs_indx;
	int mgd_mode_tx_fixed_rate;
	int mgd_mode_tx_fixed_mcs_indx;
	unsigned int peer_ampdu_factor;
	unsigned char is_associated;
	unsigned char rate_protection_type;
	unsigned char num_spatial_streams;
	unsigned char uccp_num_spatial_streams;
	unsigned char auto_sensitivity;
	/*RF Params: Input to the RF for operation*/
	unsigned char  rf_params[RF_PARAMS_SIZE];
	unsigned char  rf_params_vpd[RF_PARAMS_SIZE];
	/*Calibration Params: Input for different calibrations in RF*/
	unsigned char production_test;
	unsigned int dot11a_support;
	unsigned int dot11g_support;
	unsigned int chnl_bw;
	unsigned int prod_mode_chnl_bw_40_mhz;
	unsigned int sec_ch_offset_40_plus;
	unsigned int sec_ch_offset_40_minus;
	unsigned int prod_mode_rate_flag;
	unsigned int prod_mode_rate_preamble_type;
	unsigned int prod_mode_stbc_enabled;
	unsigned int prod_mode_bcc_or_ldpc;
	unsigned int max_tx_streams;
	unsigned int max_rx_streams;
	unsigned int max_data_size;
	unsigned int disable_power_save;
	unsigned int disable_sm_power_save;
	unsigned int max_tx_cmds;
	unsigned int prod_mode_chnl_bw_80_mhz;
	unsigned int sec_40_ch_offset_80_plus;
	unsigned int sec_40_ch_offset_80_minus;
#ifdef PERF_PROFILING
	unsigned int driver_tput;
#endif
	unsigned int disable_beacon_ibss;
	unsigned int vht_beamform_enable;
	unsigned int vht_beamform_period;
	unsigned int vht_beamform_support;
	unsigned char bg_scan_channel_list[50];
	unsigned char bg_scan_channel_flags[50];
	unsigned int bg_scan_enable;
	unsigned int bg_scan_intval;
	unsigned int bg_scan_chan_dur;
	unsigned int bg_scan_serv_chan_dur;
	unsigned int bg_scan_num_channels;
	unsigned int nw_selection;
	unsigned int hw_scan_status;
	unsigned int scan_type;
	unsigned int set_tx_power;
	unsigned int aux_adc_chain_id;
	unsigned char pdout_voltage[MAX_AUX_ADC_SAMPLES];
	char rssi_average[MAX_RSSI_SAMPLES];
	unsigned int extra_scan_ies;
	unsigned int fw_loading;
	struct wifi_sync sync[MAX_VIFS];
	unsigned int bt_state;
	unsigned int antenna_sel;
	int pkt_gen_val;
	int payload_length;
	int start_prod_mode;
	int init_prod;
};

struct cmd_send_recv_cnt {
	int tx_cmd_send_count;
	int tx_done_recv_count;
	int total_cmd_send_count;
	unsigned int outstanding_ctrl_req;
	unsigned long control_path_flags;
	spinlock_t control_path_lock;
	struct sk_buff_head outstanding_cmd;
};

struct wifi_stats {
	unsigned int ht_tx_mcs0_packet_count;
	unsigned int ht_tx_mcs1_packet_count;
	unsigned int ht_tx_mcs2_packet_count;
	unsigned int ht_tx_mcs3_packet_count;
	unsigned int ht_tx_mcs4_packet_count;
	unsigned int ht_tx_mcs5_packet_count;
	unsigned int ht_tx_mcs6_packet_count;
	unsigned int ht_tx_mcs7_packet_count;
	unsigned int ht_tx_mcs8_packet_count;
	unsigned int ht_tx_mcs9_packet_count;
	unsigned int ht_tx_mcs10_packet_count;
	unsigned int ht_tx_mcs11_packet_count;
	unsigned int ht_tx_mcs12_packet_count;
	unsigned int ht_tx_mcs13_packet_count;
	unsigned int ht_tx_mcs14_packet_count;
	unsigned int ht_tx_mcs15_packet_count;
	unsigned int vht_tx_mcs0_packet_count;
	unsigned int vht_tx_mcs1_packet_count;
	unsigned int vht_tx_mcs2_packet_count;
	unsigned int vht_tx_mcs3_packet_count;
	unsigned int vht_tx_mcs4_packet_count;
	unsigned int vht_tx_mcs5_packet_count;
	unsigned int vht_tx_mcs6_packet_count;
	unsigned int vht_tx_mcs7_packet_count;
	unsigned int vht_tx_mcs8_packet_count;
	unsigned int vht_tx_mcs9_packet_count;
	unsigned int tx_cmds_from_stack;
	unsigned int tx_dones_to_stack;
	unsigned int system_rev;
	unsigned int outstanding_cmd_cnt;
	unsigned int pending_tx_cnt;
	unsigned int umac_scan_req;
	unsigned int umac_scan_complete;
	unsigned int gen_cmd_send_count;
	unsigned int tx_cmd_send_count_single;
	unsigned int tx_cmd_send_count_multi;
	unsigned int tx_done_recv_count;
	unsigned int rx_packet_mgmt_count;
	unsigned int rx_packet_data_count;
	unsigned int ed_cnt;
	unsigned int mpdu_cnt;
	unsigned int ofdm_crc32_pass_cnt;
	unsigned int ofdm_crc32_fail_cnt;
	unsigned int dsss_crc32_pass_cnt;
	unsigned int dsss_crc32_fail_cnt;
	unsigned int mac_id_pass_cnt;
	unsigned int mac_id_fail_cnt;
	unsigned int ofdm_corr_pass_cnt;
	unsigned int ofdm_corr_fail_cnt;
	unsigned int dsss_corr_pass_cnt;
	unsigned int dsss_corr_fail_cnt;
	unsigned int ofdm_s2l_fail_cnt;
	unsigned int lsig_fail_cnt;
	unsigned int htsig_fail_cnt;
	unsigned int vhtsiga_fail_cnt;
	unsigned int vhtsigb_fail_cnt;
	unsigned int nonht_ofdm_cnt;
	unsigned int nonht_dsss_cnt;
	unsigned int mm_cnt;
	unsigned int gf_cnt;
	unsigned int vht_cnt;
	unsigned int aggregation_cnt;
	unsigned int non_aggregation_cnt;
	unsigned int ndp_cnt;
	unsigned int ofdm_ldpc_cnt;
	unsigned int ofdm_bcc_cnt;
	unsigned int midpacket_cnt;
	unsigned int dsss_sfd_fail_cnt;
	unsigned int dsss_hdr_fail_cnt;
	unsigned int dsss_short_preamble_cnt;
	unsigned int dsss_long_preamble_cnt;
	unsigned int sifs_event_cnt;
	unsigned int cts_cnt;
	unsigned int ack_cnt;
	unsigned int sifs_no_resp_cnt;
	unsigned int unsupported_cnt;
	unsigned int l1_corr_fail_cnt;
	unsigned int phy_stats_reserved22;
	unsigned int phy_stats_reserved23;
	unsigned int phy_stats_reserved24;
	unsigned int phy_stats_reserved25;
	unsigned int phy_stats_reserved26;
	unsigned int phy_stats_reserved27;
	unsigned int phy_stats_reserved28;
	unsigned int phy_stats_reserved29;
	unsigned int phy_stats_reserved30;
	unsigned int pdout_val;
	unsigned char uccp420_lmac_version[8];
	/* TX related */
	unsigned int tx_pkts_from_lmac;
	unsigned int tx_pkts_tx2tx;
	unsigned int tx_pkts_from_rx;
	unsigned int tx_pkts_ofdm;

	unsigned int tx_pkts_dsss;
	unsigned int tx_pkts_reached_end_of_fsm;
	unsigned int tx_unsupported_modulation;
	unsigned int tx_latest_pkt_from_lmac_or_sifs;

	unsigned int tx_abort_bt_confirm_cnt;       /* Tx abort due to BT
						     * confirm at the start of
						     * Txn
						     */
	unsigned int tx_abort_txstart_timeout_cnt;  /* Tx abort due to Tx start
						     * time-out
						     */
	unsigned int tx_abort_mid_bt_cnt;           /* Tx abort due to BT during
						     * WLAN txn
						     */
	unsigned int tx_abort_dac_underrun_cnt;     /* Tx abort due to DAC
						     * under-run only
						     */
	unsigned int tx_ofdm_symbols_master;
	unsigned int tx_ofdm_symbols_slave1;
	unsigned int tx_ofdm_symbols_slave2;
	unsigned int tx_dsss_symbols;
	unsigned int cts_received_mcp_cnt;

	/*MAC Stats*/
	/* TX related */
	unsigned int tx_cmd_cnt; /* Num of TX commands received from host */
	unsigned int tx_done_cnt; /* Num of Tx done events sent to host */
	unsigned int tx_edca_trigger_cnt; /* Num times EDCA engine was
					   * triggered
					   */
	unsigned int tx_edca_isr_cnt; /* Num of times EDCA ISR was generated */
	unsigned int tx_start_cnt; /* Num of TX starts to MCP */
	unsigned int tx_abort_cnt; /* Num of TX aborts detected */
	unsigned int tx_abort_isr_cnt; /* Num of TX aborts received from MCP */
	unsigned int tx_underrun_cnt; /* Num of under-runs */
	unsigned int tx_rts_cnt; /* Num of RTS frames Txd */
	unsigned int tx_ampdu_cnt; /* Num of AMPDUs txd incremented by 1 for
				    * each A-MPDU (consisting of one or more
				    * MPDUs)
				    */
	unsigned int tx_mpdu_cnt; /* Num of MPDUs txd  incremented by 1 for
				   * MPDU (1 for each A-MPDU subframe)
				   */

	/* RX related */
	unsigned int rx_isr_cnt; /* Num of RX ISRs */
	unsigned int rx_ack_cts_to_cnt; /* Num of timeouts ACK */
	unsigned int rx_cts_cnt; /* Num of CTS frames received */
	unsigned int rx_ack_resp_cnt; /* Num of Ack frames received */
	unsigned int rx_ba_resp_cnt; /* Num of BA frames received */
	unsigned int rx_fail_in_ba_bitmap_cnt; /* Num of BA frames indicating at
						* least one failure in the BA
						* bitmap
						*/
	unsigned int rx_circular_buffer_free_cnt; /* Num of entries returned to
						   * RX circular buffers
						   */
	unsigned int rx_mic_fail_cnt; /* Num of MIC failures */

	/* HAL related */
	unsigned int hal_cmd_cnt; /* Num of commands received by HAL from the
				   * host
				   */
	unsigned int hal_event_cnt; /* Num of events sent by HAL to the host */
	unsigned int hal_ext_ptr_null_cnt; /* Num of packets dropped due to lack
					    * of Ext Ram buffers from host
					    */

	/*RF Calibration Data*/
	unsigned int rf_calib_data_length;
	unsigned char rf_calib_data[MAX_RF_CALIB_DATA];
};

struct tx_config {
	/* Used to protect the TX pool */
	spinlock_t lock;

#ifdef PERF_PROFILING
	 struct timer_list persec_timer;
#endif
	/* Used to store tx tokens(buff pool ids) */
	unsigned long buf_pool_bmp[(NUM_TX_DESCS/TX_DESC_BUCKET_BOUND) + 1];

	unsigned int outstanding_tokens[NUM_ACS];
	unsigned int next_spare_token_ac;

	/* Used to store the address of pending skbs per ac */
	struct sk_buff_head pending_pkt[NUM_ACS];

	/* Used to store the address of tx'ed skb and len of 802.11 hdr
	 * it will be used in tx complete.
	 */
	struct sk_buff_head tx_pkt[NUM_TX_DESCS];
	unsigned int tx_pkt_hdr_len[NUM_TX_DESCS];

	unsigned int queue_stopped_bmp;
	struct sk_buff_head proc_tx_list[NUM_TX_DESCS];
};

enum device_state {
	STOPPED = 0,
	STARTED
};

enum tid_aggr_state {
	TID_STATE_INVALID = 0,
	TID_STATE_AGGR_START,
	TID_STATE_AGGR_STOP,
	TID_STATE_AGGR_OPERATIONAL
};

#define TID_INITIATOR_STA 0x0000
#define TID_INITIATOR_AP 0x0010

struct sta_tid_info {
	unsigned short ssn;
	enum tid_aggr_state tid_state;
};

#ifdef CONFIG_PM
struct econ_ps_cfg_status {
	unsigned char completed;
	unsigned char result;
	int wake_trig;
};
#endif

struct current_channel {
	unsigned int pri_chnl_num;
	unsigned int chnl_num1;
	unsigned int chnl_num2;
	unsigned int freq_band;
	unsigned int ch_width;
};

struct roc_params {
	unsigned char roc_in_progress;
	unsigned char roc_ps_changed;
	unsigned char roc_chan_changed;
	atomic_t roc_mgmt_tx_count;
};
struct mac80211_dev {
	struct proc_dir_entry *umac_proc_dir_entry;
	struct device *dev;
	struct mac_address if_mac_addresses[MAX_VIFS];
	unsigned int active_vifs;
	struct mutex mutex;
	int state;
	int txpower;
	unsigned char	mc_filters[MCST_ADDR_LIMIT][6];
	int mc_filter_count;

	struct tasklet_struct proc_tx_tasklet;
	/*ROC Work*/
	struct delayed_work roc_complete_work;
	struct roc_params roc_params;
	struct current_channel cur_chan;
	struct tx_config tx;
	struct sk_buff_head pending_pkt[NUM_ACS];

	/* Regulatory stuff */
	char alpha2[2]; /* alpha2 country code */
#ifdef CONFIG_PM
	struct econ_ps_cfg_status econ_ps_cfg_stats;
#endif
	struct wifi_params *params;
	struct wifi_stats  *stats;
	char name[20];
	char scan_abort_done;
	char chan_prog_done;
	char reset_complete;
	int power_save; /* Will be set only when a single VIF in
			 * STA mode is active
			 */
	struct ieee80211_vif *vifs[MAX_VIFS];
	struct ieee80211_hw *hw;
	struct sta_tid_info  tid_info[32];
	spinlock_t bcast_lock; /* Used to ensure more_frames bit is set properly
				* when transmitting bcast frames in AP in IBSS
				* modes
				*/
	unsigned char tx_antenna;
	unsigned char tx_last_beacon;
	unsigned int rts_threshold;
};

struct edca_params {
	unsigned short txop; /* units of 32us */
	unsigned short cwmin;/* units of 2^n-1 */
	unsigned short cwmax;/* units of 2^n-1 */
	unsigned char aifs;
	unsigned char uapsd;
};

struct umac_vif {
	struct timer_list bcn_timer;
#ifdef PERF_PROFILING
	struct timer_list driver_tput_timer;
#endif
	struct uvif_config {
		unsigned int atim_window;
		unsigned int aid;
		unsigned int bcn_lost_cnt;
		struct edca_params edca_params[NUM_ACS];
	} config;

	unsigned int noa_active;
	struct sk_buff_head noa_que;
	unsigned int noa_tx_allowed;

	int vif_index;
	struct ieee80211_vif *vif;
	struct mac80211_dev *dev;
	unsigned char bssid[ETH_ALEN];
	unsigned int peer_ampdu_factor;

	/*Global Sequence no for non-qos and mgmt frames/vif*/
	__u16 seq_no;
};


extern int wait_for_scan_abort(struct mac80211_dev *dev);
extern int wait_for_channel_prog_complete(struct mac80211_dev *dev);
extern int uccp420wlan_prog_nw_selection(unsigned int nw_select_enabled,
					 unsigned char *mac_addr);
extern int  uccp420wlan_core_init(struct mac80211_dev *dev, unsigned int ftm);
extern void uccp420wlan_core_deinit(struct mac80211_dev *dev, unsigned int ftm);
extern void uccp420wlan_vif_add(struct umac_vif  *uvif);
extern void uccp420wlan_vif_remove(struct umac_vif *uvif);
extern void uccp420wlan_vif_set_edca_params(unsigned short queue,
					    struct umac_vif *uvif,
					    struct edca_params *params,
					    unsigned int vif_active);
extern void uccp420wlan_vif_bss_info_changed(struct umac_vif *uvif,
					     struct ieee80211_bss_conf
					     *bss_conf, unsigned int changed);
extern int  uccp420wlan_tx_frame(struct sk_buff *skb, struct ieee80211_sta *sta,
				 struct mac80211_dev *dev, bool bcast);
extern void uccp420wlan_tx_init(struct mac80211_dev *dev);
extern void uccp420wlan_tx_deinit(struct mac80211_dev *dev);

extern void proc_bss_info_changed(unsigned char *mac_addr, int value);
extern void packet_generation(unsigned long data);
extern int wait_for_reset_complete(struct mac80211_dev *dev);

/* Beacon TimeStamp */
__s32 __attribute__((weak)) frc_to_atu(__u32 frccnt, __u64 *patu, s32 dir);
int __attribute__((weak)) get_evt_timer_freq(unsigned int *mask,
						unsigned int *num,
						unsigned int *denom);

extern unsigned char *rf_params_vpd;
extern int num_streams_vpd;

static inline int vif_addr_to_index(unsigned char *addr,
				    struct mac80211_dev *dev)
{
	int i;
	for (i = 0; i < MAX_VIFS; i++)
		if (ether_addr_equal(addr, dev->if_mac_addresses[i].addr))
			break;
	if ((i < MAX_VIFS) && (dev->active_vifs & (1 << i)))
		return i;
	else
		return -1;
}

static inline int ieee80211_is_unicast_robust_mgmt_frame(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (skb->len < 24 || is_multicast_ether_addr(hdr->addr1))
		return 0;

	return ieee80211_is_robust_mgmt_frame(skb);
}
static inline bool is_bufferable_mgmt_frame(struct ieee80211_hdr *hdr)
{
	__u16 fc = hdr->frame_control;
	/*TODO: Handle Individual Probe Response frame in IBSS*/
	if (ieee80211_is_action(fc) ||
		ieee80211_is_disassoc(fc) ||
		ieee80211_is_deauth(fc))
		return	true;

	return false;
}
#endif /* _UCCP420WLAN_CORE_H_ */
