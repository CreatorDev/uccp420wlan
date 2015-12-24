/*
 * File Name  : host_umac_if.h
 *
 * This file contains the UMAC<-->HOST comms data structures
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

#ifndef _UCCP420HOST_UMAC_IF_H_
#define _UCCP420HOST_UMAC_IF_H_

#include "hal.h"
#define MCST_ADDR_LIMIT	48
#define WLAN_ADDR_LEN 6
#define TKIP_MIC_LEN 8
#define MICHAEL_LEN 16
#define MAX_KEY_LEN 16
#define MAX_VIFS 2

#define MAX_PEERS 15
/* Additional queue for unicast frames directed to non-associated peers (for
 * e.g. Probe Responses etc)
 */
#define MAX_PEND_Q_PER_AC (MAX_PEERS + MAX_VIFS)

#ifdef MULTI_CHAN_SUPPORT
#define MAX_CHANCTX MAX_VIFS
#define MAX_OFF_CHANCTX MAX_VIFS
#define OFF_CHANCTX_IDX_BASE MAX_CHANCTX
#endif

#define WEP40_KEYLEN 5
#define WEP104_KEYLEN 13
#define MAX_WEP_KEY_LEN 13

#define WLAN_20MHZ_OPERATION 0
#define WLAN_40MHZ_OPERATION 1
#define WLAN_80MHZ_OPERATION 2
#define WLAN_SEC_UPPER 0
#define WLAN_SEC_LOWER 1

/* TEMPdec */
#define PWRSAVE_STATE_AWAKE 1
#define PWRSAVE_STATE_DOZE 0
/* TEMPDEC */
#define MAX_SSID_LEN 32
#define MAX_NUM_SSIDS 4
#define TOTAL_KEY_LEN 32
#define RX_SEQ_SIZE 6
#define MAX_IE_LEN 100
#define ETH_ALEN 6

#define MAX_TX_CMDS 32
#define MAX_GRAM_PAYLOAD_LEN 52

#define RF_PARAMS_SIZE	369
#define MAX_RF_CALIB_DATA 900
struct hal_data {
	unsigned char hal_data[HAL_PRIV_DATA_SIZE];
} __packed;

struct host_mac_msg_hdr {
	struct hal_data hal_data;
	unsigned int descriptor_id; /* LSB 2 bytes as pool id, MSB 2 bytes
				     * queue num, pool ID of 0xFFFF indicates
				     * no payload
				     */
	unsigned int payload_length;
	unsigned int id;
	unsigned int length;
	unsigned int more_cmd_data; /* used for fragmenting commands */
} __packed;


enum UMAC_QUEUE_NUM {
	WLAN_AC_BK = 0,
	WLAN_AC_BE,
	WLAN_AC_VI,
	WLAN_AC_VO,
	WLAN_AC_BCN,
	WLAN_AC_MAX_CNT
};


enum UMAC_EVENT_ROC_STAT {
	UMAC_ROC_STAT_STARTED,
	UMAC_ROC_STAT_STOPPED,
	UMAC_ROC_STAT_DONE,
	UMAC_ROC_STAT_ABORTED
};

enum UMAC_VIF_CHANCTX_TYPE {
	UMAC_VIF_CHANCTX_TYPE_OPER,
	UMAC_VIF_CHANCTX_TYPE_OFF,
	MAX_UMAC_VIF_CHANCTX_TYPES
};

struct umac_event_tx_done {
	struct host_mac_msg_hdr hdr;

	unsigned char pdout_voltage;
	/* frame_status -
	 * 0 - success
	 * 1 - discarded due to retry limit exceeded
	 * 2 - discarded due to msdu lifetime expiry
	 * 3 - discarded due to encryption key not available
	 */
#define TX_DONE_STAT_SUCCESS (0)
#define TX_DONE_STAT_ERR_RETRY_LIM (1)
#define TX_DONE_STAT_MSDU_LIFETIME (2)
#define TX_DONE_STAT_KEY_NOT_FOUND (3)
#define TX_DONE_STAT_DISCARD (4)
#define TX_DONE_STAT_DISCARD_BCN (5)
#ifdef MULTI_CHAN_SUPPORT
#define TX_DONE_STAT_DISCARD_CHSW (6)
#endif
#define TX_DONE_STAT_DISCARD_OP_TX (7)
	unsigned char frm_status[MAX_TX_CMDS];
	unsigned char retries_num[MAX_TX_CMDS];
	/* rate = Units of 500 Kbps or mcs index = 0 to 7*/
	unsigned char rate[MAX_TX_CMDS];
	unsigned char queue;
	unsigned int descriptor_id;
	unsigned char reserved[12];
} __packed;

struct umac_event_ch_prog_complete {
	struct host_mac_msg_hdr hdr;
} __packed;

#ifdef MULTI_CHAN_SUPPORT
struct umac_event_ch_switch {
	struct host_mac_msg_hdr hdr;
	int chan;
} __packed;
#endif

struct umac_event_noa {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	unsigned char vif_addr[ETH_ALEN];

	/* 1 indicates NoA feature is active
	 * 0 indicates NoA feature is not active
	 */
	unsigned int noa_active;
#define ABSENCE_START 0 /* Indicates AP is absent */
#define ABSENCE_STOP 1 /* Indicates AP is present */
	unsigned int ap_present;
} __packed;

struct umac_event_mib_stats {
	struct host_mac_msg_hdr hdr;
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
	/*Tx Stats*/
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
} __packed;


struct umac_event_mac_stats {
	struct host_mac_msg_hdr hdr;
	unsigned int roc_start;
	unsigned int roc_stop;
	unsigned int roc_complete;
	unsigned int roc_stop_complete;
	/* TX related */
	unsigned int tx_cmd_cnt; /* Num of TX commands received from host */
	unsigned int tx_done_cnt; /* Num of Tx done events sent to host */
	unsigned int tx_edca_trigger_cnt; /* Num of times EDCA engine was
					   * triggered
					   */
	unsigned int tx_edca_isr_cnt; /* Num of times EDCA ISR was generated */
	unsigned int tx_start_cnt; /* Num of TX starts to MCP */
	unsigned int tx_abort_cnt; /* Num of TX aborts detected */
	unsigned int tx_abort_isr_cnt;/* Num of TX aborts received from MCP */
	unsigned int tx_underrun_cnt; /* Num of under-runs */
	unsigned int tx_rts_cnt; /* Num of RTS frames Tx’d */
	unsigned int tx_ampdu_cnt; /* Num of AMPDU’s tx’d –incremented by
				    * 1 for each A-MPDU (consisting of one or
				    * more MPDUs)
				    */
	unsigned int tx_mpdu_cnt; /* Num of MPDU’s tx’d – incremented by 1
				   * for MPDU (1 for each A-MPDU subframe)
				   */
	/* RX related */
	unsigned int rx_isr_cnt; /* Num of RX ISRs */
	unsigned int rx_ack_cts_to_cnt; /* Num of timeouts ACK */
	unsigned int rx_cts_cnt; /* Num of CTS frames received */
	unsigned int rx_ack_resp_cnt; /* Num of Ack frames received */
	unsigned int rx_ba_resp_cnt; /* Num of BA frames received */
	unsigned int rx_fail_in_ba_bitmap_cnt; /* Num of BA frames indicating at
						* least one failure in the
						* BA bitmap
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

} __packed;


struct wlan_rx_pkt {
	struct host_mac_msg_hdr hdr;
	/* MPDU/MSDU payload in bytes */
	unsigned int pkt_length;
	/* bit[8] = 0 - legacy data rate
	 *	  = 1 - MCS index
	 */
	unsigned char rate_or_mcs;
	/* RSSI in dbm */
	unsigned char rssi;
	/* packet status
	 * 1 - mic failed
	 * 0 - mic succes reserved for non encryped packet\
	 */
#define RX_MIC_SUCCESS 0 /* No MIC error in frame */
#define RX_MIC_FAILURE_TKIP 1 /* TKIP MIC error in frame */
#define RX_MIC_FAILURE_CCMP 2 /* CCMP MIC error in frame */
	unsigned char rx_pkt_status;

#define ENABLE_GREEN_FIELD 0x01
#define ENABLE_CHNL_WIDTH_40MHZ 0x02
#define ENABLE_SGI 0x04
#define ENABLE_11N_FORMAT 0x08
#define ENABLE_VHT_FORMAT 0x10
#define ENABLE_CHNL_WIDTH_80MHZ 0x20

	unsigned char rate_flags;
	unsigned char nss;
	unsigned char num_sts;
	unsigned char timestamp[8];
	unsigned char stbc_enabled;
	unsigned char ldpc_enabled;
	unsigned char link_margin;
	unsigned char channel;
/* Currently size of reserved =
 * sizeof(beacon_time_stamp = 8) +
 * sizeof(ets_timer = 4) +
 * sizeof(delta_timer_diff = 4) +
 * (qos_padding = 2)
 */
	unsigned char reserved[18];
	/*payload bytes */
	unsigned char payload[0];
} __packed;

#ifdef CONFIG_PM
enum UMAC_PS_ECON_WAKE_TRIG {
	TRIG_PKT_RCV,
	TRIG_DISCONNECT
};

struct umac_event_ps_econ_wake {
	struct host_mac_msg_hdr hdr;
	enum UMAC_PS_ECON_WAKE_TRIG trigger;
} __packed;

struct umac_event_ps_econ_cfg_complete {
	struct host_mac_msg_hdr hdr;
	unsigned char status; /* SUCCESS/FAILURE */
} __packed;
#endif



enum UMAC_CMD_TAG {
	UMAC_CMD_RESET = 0,
	UMAC_CMD_SCAN,
	UMAC_CMD_SCAN_ABORT,
	UMAC_CMD_CONNECT,
	UMAC_CMD_SETKEY,
	UMAC_CMD_SET_DEFAULTKEY,
	UMAC_CMD_REKEY_DATA,
	UMAC_CMD_TX,
	UMAC_CMD_MGMT_TX,
	UMAC_CMD_FRAG,
	UMAC_CMD_TX_POWER,
	UMAC_CMD_RATE,
	UMAC_CMD_DISCONNECT,
	UMAC_CMD_PS,
	UMAC_CMD_PS_ECON_CFG,
	UMAC_CMD_VIF_CTRL,
	UMAC_CMD_SET_BEACON,
	UMAC_CMD_SET_MODE,
	UMAC_CMD_BA_SESSION_INFO,
	UMAC_CMD_MCST_ADDR_CFG,
	UMAC_CMD_MCST_FLTR_CTRL,
	UMAC_CMD_VHT_BEAMFORM_CTRL,
	UMAC_CMD_ROC_CTRL,
	UMAC_CMD_CHANNEL,
	UMAC_CMD_VIF_CFG,
	UMAC_CMD_STA,
	UMAC_CMD_TXQ_PARAMS,
	UMAC_CMD_MIB_STATS,
	UMAC_CMD_PHY_STATS,
	UMAC_CMD_NW_SELECTION,
	UMAC_CMD_AUX_ADC_CHAIN_SEL,
	UMAC_CMD_DETECT_RADAR,
	UMAC_CMD_ENABLE_TX,
	UMAC_CMD_DISCARD_PKTS,
	UMAC_CMD_MEASURE,
	UMAC_CMD_BT_INFO,
	UMAC_CMD_CLEAR_STATS,
#ifdef MULTI_CHAN_SUPPORT
	UMAC_CMD_CHANCTX_TIME_INFO,
#endif
	UMAC_CMD_CONT_TX,
};

enum UMAC_EVENT_TAG {
	UMAC_EVENT_RX = 0,
	UMAC_EVENT_TX_DONE,
	UMAC_EVENT_DISCONNECTED,
	UMAC_EVENT_CONNECT_RESULT,
	UMAC_EVENT_MIC_FAIL,
	UMAC_EVENT_SCAN_COMPLETE,
	UMAC_EVENT_SCAN_ABORT_COMPLETE,
	UMAC_EVENT_MGMT_FRAME,
	UMAC_EVENT_RESET_COMPLETE,
	UMAC_EVENT_RSSI,
	UMAC_EVENT_STA_INFO,
	UMAC_EVENT_REKEY_DATA,
	UMAC_EVENT_MIB_STAT,
	UMAC_EVENT_PHY_STAT,
	UMAC_EVENT_NW_FOUND,
	UMAC_EVENT_NOA,
	UMAC_EVENT_CTRL_POOL_ACK,
	UMAC_EVENT_COMMAND_PROC_DONE,
	UMAC_EVENT_CH_PROG_DONE,
	UMAC_EVENT_PS_ECON_CFG_DONE,
	UMAC_EVENT_PS_ECON_WAKE,
	UMAC_EVENT_MAC_STATS,
	UMAC_EVENT_RF_CALIB_DATA,
	UMAC_EVENT_RADAR_DETECTED,
	UMAC_EVENT_MSRMNT_COMPLETE,
	UMAC_EVENT_ROC_STATUS,
#ifdef MULTI_CHAN_SUPPORT
	UMAC_EVENT_CHAN_SWITCH,
#endif
};

enum CONNECT_RESULT_TAG {
	CONNECT_SUCCESS = 0,
	CONNECT_UNSPECIFIED_FAILURE,
	CONNECT_AUTH_FAILURE,
	CONNECT_AUTH_TIMEOUT,
	CONNECT_ASSOC_TIMEOUT,
	CONNECT_ASSOC_FAILURE,
	CONNECT_START_IBSS
};

enum UMAC_TX_FLAGS {
	UMAC_TX_FLAG_OFFCHAN_FRM
};

/* Commands */
struct cmd_tx_ctrl {
	struct host_mac_msg_hdr hdr;
	/* VIF nuber this packet belongs to */
	unsigned char if_index;
	/* Queue no will be VO, VI, BE, BK and BCN */
	unsigned char queue_num;

	unsigned int descriptor_id;

	/* number of frames in tx descriptors */
	unsigned int num_frames_per_desc;

	/*packet lengths of frames*/
	unsigned int pkt_length[MAX_TX_CMDS];

	/* If more number of frames buffered at UMAC */
	unsigned char more_frms;

	/* If this field is set for any packet,
	 * need to be transmit even though TX has been disabled
	 */
	unsigned int force_tx;

	/* Flags to communicate special cases regarding the frame to the FW */
	unsigned int tx_flags;

	unsigned char num_rates;

#define USE_PROTECTION_NONE 0
#define USE_PROTECTION_RTS 1
#define USE_PROTECTION_CTS2SELF 2
	unsigned char rate_protection_type[4];

#define USE_SHORT_PREAMBLE 0
#define DONT_USE_SHORT_PREAMBLE 1
	unsigned char rate_preamble_type[4];

	unsigned char rate_retries[4];

#define MARK_RATE_AS_MCS_INDEX 0x80
#define MARK_RATE_AS_RATE 0x00
	unsigned char rate[4];

#define ENABLE_GREEN_FIELD 0x01
#define ENABLE_CHNL_WIDTH_40MHZ 0x02
#define ENABLE_SGI 0x04
#define ENABLE_11N_FORMAT 0x08
#define ENABLE_VHT_FORMAT 0x10
#define ENABLE_CHNL_WIDTH_80MHZ 0x20

	unsigned char rate_flags[4];
	unsigned char num_spatial_streams[4];
	unsigned char stbc_enabled;
	unsigned char bcc_or_ldpc;

#define AMPDU_AGGR_ENABLED 0x00000001
#define AMPDU_AGGR_DISABLED 0x00000000
	unsigned char aggregate_mpdu;

	unsigned char force_encrypt;

#define MAC_HDR_SIZE 52
	unsigned int pkt_gram_payload_len;
	/* It will be of the form It [MAX_TX_CMDS][54]
	 * using dynamic because max stack size is 1024 bytes
	 */
	unsigned char gram_payload[0];
} __packed;

struct bgscan_params {
	unsigned int enabled;
	unsigned char channel_list[50];
	unsigned char channel_flags[50];
	unsigned int scan_intval;
	unsigned int channel_dur;
	unsigned int serv_channel_dur;
	unsigned int num_channels;
} __packed;

struct cmd_reset {
	struct host_mac_msg_hdr hdr;
	#define LMAC_ENABLE 0
	#define LMAC_DISABLE 1
	unsigned int type;
	int ed_sensitivity;
	unsigned int auto_sensitivity;
	unsigned char rf_params[RF_PARAMS_SIZE];
	unsigned int include_rxmac_hdr;
	struct bgscan_params bg_scan;
	unsigned char num_spatial_streams;
	unsigned int system_rev;
	#define LMAC_MODE_NORMAL 0
	#define LMAC_MODE_FTM 1
	unsigned int lmac_mode;
	unsigned int antenna_sel;
} __packed;

enum SCAN_TYPE_TAG {
	PASSIVE = 0,
	ACTIVE
};

struct ssid {
	unsigned int len;
	unsigned char ssid[MAX_SSID_LEN];
} __packed;

struct cmd_scan {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	enum SCAN_TYPE_TAG type;

	/* Total number of channels to scan; channel numbers will be
	 * informed in channel array. if n_channel value is zero,
	 * UMAC scans all possible channels.
	 */
	unsigned int n_channel;

	/* Number of SSIDs to scan; ssid information will be in ssid array.
	 * This is always >= 1. In case of wild card SSID, this value is 1 and
	 * the ssid_len of the first entry in the SSID list should be specifie
	 * as 0
	 */
	unsigned int n_ssids;
	unsigned char channel_list[50];
	unsigned char chan_max_power[50];
	unsigned char chan_flags[50];
	struct ssid ssids[MAX_NUM_SSIDS];
	unsigned int p2p_probe;
	unsigned int extra_ies_len;
	unsigned char extra_ies[0];
} __packed;

struct cmd_scan_abort {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
} __packed;

struct cmd_nw_selection {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	unsigned int p2p_selection;
	struct ssid ssid;
	unsigned int scan_req_ie_len;
	unsigned int scan_resp_ie_len;
	unsigned char scan_req_ie[200];
	unsigned char scan_resp_ie[200];
} __packed;

struct cmd_set_mode {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	unsigned int type;
} __packed;

struct cmd_setkey {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
#define KEY_CTRL_ADD 0
#define KEY_CTRL_DEL 1
	unsigned int ctrl;
#define KEY_TYPE_UCAST 0
#define KEY_TYPE_BCAST 1
	unsigned int key_type;

#define CIPHER_TYPE_WEP40 0
#define CIPHER_TYPE_WEP104 1
#define CIPHER_TYPE_TKIP 2
#define CIPHER_TYPE_CCMP 3
#define CIPHER_TYPE_WAPI 4
	unsigned int cipher_type;
	unsigned int key_id;
	int key_len;
	int rsc_len;
	unsigned char mac_addr[ETH_ALEN];
	unsigned char key[TOTAL_KEY_LEN];
	unsigned char rsc[RX_SEQ_SIZE];
} __packed;


struct cmd_set_defaultkey {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	unsigned int key_id;
} __packed;

struct cmd_set_rekey {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	unsigned char kek[16];
	unsigned char kck[16];
	unsigned char replay_ctr[8];
} __packed;

struct cmd_frag_tag {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	unsigned int frag_threshold;
} __packed;

struct cmd_tx_pwr {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	int tx_pwr;
} __packed;

struct cmd_disconnect {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	int reason_code;
} __packed;

struct cmd_rate {
	struct host_mac_msg_hdr hdr;
	int is_mcs;
	int rate;
} __packed;

struct cmd_mcst_addr_cfg {
	struct host_mac_msg_hdr hdr;
	/* mcst_ctrl -
	 * 0 -- ADD multicast address
	 * 1 -- Remove multicast address
	 */
#define WLAN_MCAST_ADDR_ADD 0
#define WLAN_MCAST_ADDR_REM 1
	unsigned int op;
	/* addr to add or delete..
	 */
	unsigned char mac_addr[6];
} __packed;

struct cmd_mcst_filter_ctrl {
	struct host_mac_msg_hdr hdr;
	/* ctrl -
	 * 0 - disable multicast filtering in LMAC
	 * 1 - enable multicast filtering in LMAC
	 */
#define MCAST_FILTER_DISABLE 0
#define MCAST_FILTER_ENABLE 1
	unsigned int ctrl;
} __packed;

struct cmd_vht_beamform {
	struct host_mac_msg_hdr hdr;
#define VHT_BEAMFORM_DISABLE 0
#define VHT_BEAMFORM_ENABLE 1
	unsigned int vht_beamform_status;
	unsigned int vht_beamform_period;
} __packed;

struct cmd_roc {
	struct host_mac_msg_hdr hdr;
#define ROC_STOP 0
#define ROC_START 1
	unsigned int roc_ctrl;
	unsigned int roc_channel;
	unsigned int roc_duration;
#define ROC_TYPE_NORMAL 0
#define ROC_TYPE_OFFCHANNEL_TX 1
	unsigned int roc_type;
} __packed;

enum POWER_SAVE_TAG {
	AWAKE = 0,
	SLEEP
};

struct cmd_ps {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	enum POWER_SAVE_TAG mode;
} __packed;

struct cmd_vifctrl {
	struct host_mac_msg_hdr hdr;
	/* if_ctrl -
	 * 0 - add interface address
	 * 1 - remove interface address
	 */
#define IF_ADD 1
#define IF_REM 2

	unsigned int if_ctrl;
	unsigned int if_index;
	/* Interface mode -
	 * 0 - STA in infrastucture mode
	 * 1 - STA in AD-HOC mode
	 * 2 - AP
	 */
#define IF_MODE_STA_BSS 0
#define IF_MODE_STA_IBSS 1
#define IF_MODE_AP 2
#define IF_MODE_INVALID 3

	unsigned int mode;
	unsigned char mac_addr[ETH_ALEN];
} __packed;

struct cmd_set_beacon {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	unsigned int interval;
	unsigned int dtim_period;
	unsigned int len;
	unsigned char mac_addr[6];
	unsigned int channel;
	unsigned char beacon_buf[0];
} __packed;

struct cmd_ht_ba {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
#define BLOCK_ACK_SESSION_STOP 0
#define BLOCK_ACK_SESSION_START 1
	unsigned int op;
	unsigned int tid;
	unsigned int ssn;
	unsigned int policy;
	/* vif address */
	unsigned char vif_addr[ETH_ALEN];
	/* peer address */
	unsigned char peer_addr[ETH_ALEN];
} __packed;

struct cmd_channel {
	struct host_mac_msg_hdr hdr;
	/* channel bw
	 * 0 - 20
	 * 1 - 40
	 * 2 - 80
	 * 3 - 160
	 */
	unsigned int channel_bw;
	unsigned int primary_ch_number;
	/* center frequecny of total band, if toal band is contiguous.
	 * First band center frequency For non contiguous bands,
	 */
	unsigned int channel_number1;
	/* center frequecny of secondary band.
	 * This is valid in 80+80 band set to zero for other cases
	 */
	unsigned int channel_number2;
	/* 0 - 2.4ghz
	 * 1 - 5ghz
	 */
	unsigned int freq_band;
#ifdef MULTI_CHAN_SUPPORT
	unsigned int vif_index;
#endif
} __packed;

struct cmd_vif_cfg {
	struct host_mac_msg_hdr hdr;

	/* Bitmap indicating whether value is changed or not */
#define BASICRATES_CHANGED (1<<0)
#define SHORTSLOT_CHANGED (1<<1)
#define POWERSAVE_CHANGED (1<<2) /* to be removed */
#define UAPSDTYPE_CHANGED (1<<3) /* to be removed */
#define ATIMWINDOW_CHANGED (1<<4)
#define AID_CHANGED (1<<5)
#define CAPABILITY_CHANGED (1<<6)
#define SHORTRETRY_CHANGED (1<<7)
#define LONGRETRY_CHANGED (1<<8)
#define BSSID_CHANGED (1<<9)
#define RCV_BCN_MODE_CHANGED (1<<10)
#define BCN_INT_CHANGED (1<<11)
#define DTIM_PERIOD_CHANGED (1<<12)
#define SMPS_CHANGED (1<<13)
#define CONNECT_STATE_CHANGED (1<<14)
#define OP_CHAN_CHANGED (1<<15)

	unsigned int changed_bitmap;

	/* bitmap of supported basic rates
	 */
	unsigned int basic_rate_set;

	/* slot type -
	 * 0 - long slot
	 * 1 - short slot
	 */
	unsigned int use_short_slot;

	/* ATIM window */
	unsigned int atim_window;

	unsigned int aid;

	unsigned int capability;

	unsigned int short_retry;

	unsigned int long_retry;

#define RCV_ALL_BCNS 0
#define RCV_ALL_NETWORK_ONLY 1
#define RCV_NO_BCNS 2

	unsigned int bcn_mode;

	unsigned char dtim_period;

	unsigned int beacon_interval;

	/* index of the intended interface */
	unsigned int if_index;
	unsigned char vif_addr[ETH_ALEN];

	/* bssid of interface */
	unsigned char bssid[ETH_ALEN];

	/* SMPS Info
	 *
	 * bit0 - 0 - Disabled, 1 - Enabled
	 * bit1 - 0 - Static,   1 - Dynamic
	 *
	 */
#define SMPS_ENABLED BIT(0)
#define SMPS_MODE BIT(1)
	unsigned char smps_info;

#define STA_CONN 0
#define STA_DISCONN 1
	unsigned char connect_state;
	unsigned char op_channel;
} __packed;

struct cmd_sta {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
#define ADD 0
#define REM 1
	unsigned int op;
#define STA_NUM_BANDS 2
	unsigned int supp_rates[STA_NUM_BANDS];
/*HT Info */
	unsigned int ht_cap; /* use IEEE80211_HT_CAP_ */
	unsigned int ht_supported;
	unsigned int vht_cap; /* use IEEE80211_VHT_CAP_ */
	unsigned int vht_supported;
	unsigned int ampdu_factor;
	unsigned int ampdu_density;
	unsigned int rx_highest;
	unsigned int tx_params;
#define HT_MCS_MASK_LEN 10
	unsigned char rx_mask[HT_MCS_MASK_LEN];
	unsigned char addr[ETH_ALEN];
	unsigned char dot11_mode;
	unsigned char no_of_streams;
	unsigned char preamble;
	unsigned char stbc_enable;
	unsigned char ldpc_enable;
	unsigned char guard_interval;
	unsigned char aggregation;
	unsigned char tid;
	unsigned char band_width;
} __packed;

struct cmd_txq_params {
	struct host_mac_msg_hdr hdr;
	unsigned int queue_num;
	unsigned int aifsn;
	unsigned int txop;
	unsigned int cwmin;
	unsigned int cwmax;
	/* power save mode -
	 * 0 - indicates legacy mode powersave, 1 - indicates UAPSD for the
	 * corresponding AC.
	 */
	unsigned int uapsd;
	unsigned int if_index;
	unsigned char vif_addr[ETH_ALEN];
} __packed;

struct cmd_aux_adc_chain_sel {
	struct host_mac_msg_hdr hdr;
#define AUX_ADC_CHAIN1	1
#define AUX_ADC_CHAIN2	2
	unsigned int chain_id;
} __packed;

struct cmd_cont_tx {
	struct host_mac_msg_hdr hdr;
	unsigned int op;
} __packed;



/* DFS SUPPORT */
/* Command to start/stop Radar detection operation */
struct cmd_detect_radar {
	struct host_mac_msg_hdr hdr;
	/* 1 - Radar detection operation to be started
	 * 0 - Radar detection operation to be stopped
	 */
#define RADAR_DETECT_OP_START 1
#define RADAR_DETECT_OP_STOP 0
	unsigned int radar_detect_op;
} __packed;

/* Command to enable TX.which would have been disabled previously.*/
struct umac_cmd_tx_enable {
	struct host_mac_msg_hdr hdr;
} __packed;

/* Command to discard all packets in TX queue */
struct cmd_discard_pkts {
	struct host_mac_msg_hdr hdr;
} __packed;

/* Command to do measurement on a channel
 * start_time	: when to start the measurement.
 * msr_dur	: How long measurement to be carried out.
 */
struct cmd_msrmnt_start {
	struct host_mac_msg_hdr hdr;
	unsigned char start_time[8];
	unsigned short msr_dur;
} __packed;


#ifdef MULTI_CHAN_SUPPORT
struct chanctx_time_info {
	int chan;
	int percentage;
};

struct cmd_chanctx_time_config {
	struct host_mac_msg_hdr hdr;
	struct chanctx_time_info info[MAX_CHANCTX];
} __packed;
#endif

/* Events */

struct nw_found_event {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	struct ssid ssid;
} __packed;

struct host_event_mgmt_rx {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	int rssi;
	unsigned int rate;
	unsigned int padding;
} __packed;

struct host_event_command_complete {
	struct host_mac_msg_hdr hdr;
} __packed;

struct bssres {
	unsigned int channel;
	int rssi;
	unsigned int frame_len;
	unsigned char frame_buf[0];
} __packed;

struct host_event_scanres {
	struct host_mac_msg_hdr hdr;
	int if_index;
	unsigned int scanres_len; /* This will include total length of
				   * scanresult, including it's own length
				   */
	unsigned int status_code;
	unsigned int no_of_bss;
	unsigned int more_results; /* 0 - No more results, 1- Moreresults */
	unsigned char bss_res[0]; /* One or more elements of type bssres_t */
} __packed;

struct host_event_connect_result {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	enum CONNECT_RESULT_TAG result_code;
	unsigned int aid;
	unsigned int cap_info;
	int ht_supported;
	unsigned short ht_cap_info;
	int vht_supported;
	unsigned short vht_cap_info;
	unsigned int qos_capability;
	unsigned int wmm_acm;
	unsigned int channel;
	unsigned char bssid[ETH_ALEN];
	unsigned int ie_len;
	unsigned char ie[MAX_IE_LEN];
	struct bssres bss_frame;
} __packed;

struct host_event_rssi {
	struct host_mac_msg_hdr hdr;
	int if_index;
	int rssi;
} __packed;

struct host_event_disconnect {
	struct host_mac_msg_hdr hdr;
	int if_index;
#define REASON_DEAUTH 1
#define REASON_AUTH_FAILURE 2
#define REASON_NW_LOST 3
#define REASON_AUTH_TIMEOUT 4
#define REASON_TX_TOKEN_NOTAVAIL 5
#define REASON_ASSOC_TIMEOUT 6
	unsigned int reason_code;
	unsigned char mac_addr[ETH_ALEN];
} __packed;

struct host_event_reset_complete {
	struct host_mac_msg_hdr hdr;
	unsigned int cap;
	unsigned int ht_supported;
	unsigned int ampdu_factor;
	unsigned int ampdu_density;
#define HT_MCS_MASK_LEN 10
	unsigned int rx_mask[HT_MCS_MASK_LEN];
	unsigned int rx_highest;
	unsigned int tx_params;
	char version[6];
} __packed;

struct host_event_rekey_data {
	struct host_mac_msg_hdr hdr;
	unsigned int if_index;
	unsigned int cipher;
	unsigned int key_idx;
	unsigned int key_len;
	unsigned int rsc_len;
	unsigned char rsc[8];
	unsigned char key[MAX_KEY_LEN];
} __packed;

struct host_event_phy_stats {
	struct host_mac_msg_hdr hdr;
	unsigned int phy_stats[64];
} __packed;

/* DFS SUPPORT*/
/* Event to be generated on radar detection */
struct host_event_radar_detected {
	struct host_mac_msg_hdr hdr;
	int freq;
} __packed;

/* Event generated on measurement completion with measurement status */
struct host_event_msrmnt_complete {
	struct host_mac_msg_hdr hdr;
	/* MSR MNT STAUTS -
	 * For bit representation Refer 7.3.2.22.1 of Std 802.11h-2003
	 */
#define UMAC_EVENT_MSRMNT_STATUS_BSS (0x01)
#define UMAC_EVENT_MSRMNT_STATUS_OFDM_PREAMBLE (0x02)
#define UMAC_EVENT_MSRMNT_STATUS_UNIDENTIFIED_SIGNAL (0x04)
#define UMAC_EVENT_MSRMNT_STATUS_RADAR_SIGNAL (0x08)
#define UMAC_EVENT_MSRMNT_STATUS_NO_MSRMNT (0x10)
#define UMAC_EVENT_MSRMNT_STATUS_LATE (0x20)
#define UMAC_EVENT_MSRMNT_STATUS_INCAPABLE (0x40)
#define UMAC_EVENT_MSRMNT_STATUS_REFUSE (0x80)
	unsigned int msrmnt_status;
} __packed;


struct umac_event_ch_switch_complete {
	struct host_mac_msg_hdr hdr;
	int status;
} __packed;

struct umac_event_rf_calib_data {
	struct host_mac_msg_hdr hdr;
	unsigned int  rf_calib_data_length;
	unsigned char rf_calib_data[0];
} __packed;

struct cmd_bt_info {
	struct host_mac_msg_hdr hdr;
#define BT_STATE_OFF 0
#define BT_STATE_ON  1
	unsigned int bt_state;
} __packed;

struct umac_event_roc_status {
	struct host_mac_msg_hdr hdr;
	unsigned int roc_status;
} __packed;

#endif /*_UCCP420HOST_UMAC_IF_H_*/
