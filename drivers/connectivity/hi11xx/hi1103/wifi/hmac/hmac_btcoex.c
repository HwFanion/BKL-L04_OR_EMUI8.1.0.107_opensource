

#ifndef __HMAC_BTCOEX_C__
#define __HMAC_BTCOEX_C__

#ifdef __cplusplus
    #if __cplusplus
        extern "C" {
    #endif
#endif

#ifdef _PRE_WLAN_FEATURE_BTCOEX

/*****************************************************************************
  1 头文件包含
*****************************************************************************/
#include "hmac_ext_if.h"
#include "mac_data.h"
#include "hmac_resource.h"
#include "hmac_btcoex.h"
#include "hmac_vap.h"
#include "hmac_user.h"
#include "hmac_fsm.h"
#include "hmac_mgmt_sta.h"
#include "hmac_mgmt_bss_comm.h"
#ifdef _PRE_WLAN_FEATURE_ROAM
#include "hmac_roam_main.h"
#endif

#undef  THIS_FILE_ID
#define THIS_FILE_ID OAM_FILE_ID_HMAC_BTCOEX_C

/*****************************************************************************
  2 函数声明
*****************************************************************************/

/*****************************************************************************
  3 函数实现
*****************************************************************************/


OAL_STATIC oal_uint32 hmac_btcoex_delba_foreach_tid(mac_vap_stru *pst_mac_vap, mac_user_stru *pst_mac_user, mac_cfg_delba_req_param_stru *pst_mac_cfg_delba_param)
{
    oal_uint32 ul_ret = 0;

    oal_set_mac_addr(pst_mac_cfg_delba_param->auc_mac_addr, pst_mac_user->auc_user_mac_addr);
    pst_mac_cfg_delba_param->en_trigger  = MAC_DELBA_TRIGGER_BTCOEX;

    for (pst_mac_cfg_delba_param->uc_tidno = 0; pst_mac_cfg_delba_param->uc_tidno < WLAN_TID_MAX_NUM; pst_mac_cfg_delba_param->uc_tidno++)
    {
        ul_ret = hmac_config_delba_req(pst_mac_vap, 0, (oal_uint8 *)pst_mac_cfg_delba_param);
        if (OAL_SUCC != ul_ret)
        {
            OAM_WARNING_LOG2(pst_mac_vap->uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_delba_foreach_tid::ul_ret: %d, tid: %d}", ul_ret, pst_mac_cfg_delba_param->uc_tidno);
            return ul_ret;
        }
    }

    return ul_ret;
}


OAL_STATIC oal_uint32 hmac_btcoex_delba_from_user(mac_vap_stru *pst_mac_vap, hmac_user_stru *pst_hmac_user)
{
    mac_cfg_delba_req_param_stru st_mac_cfg_delba_param;
    hmac_vap_stru                *pst_hmac_vap;

    OAL_MEMZERO((oal_uint8*)&st_mac_cfg_delba_param, OAL_SIZEOF(st_mac_cfg_delba_param));

    st_mac_cfg_delba_param.en_direction = MAC_RECIPIENT_DELBA;

    pst_hmac_vap = (hmac_vap_stru *)mac_res_get_hmac_vap(pst_mac_vap->uc_vap_id);
    if (OAL_PTR_NULL == pst_hmac_vap)
    {
        OAM_ERROR_LOG0(pst_mac_vap->uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_delba_from_user::pst_hmac_vap null.}");
        return OAL_ERR_CODE_PTR_NULL;
    }

    /* 属于黑名单AP，并且已经处于电话业务时，不进行删BA逻辑 */
    if (OAL_FALSE == pst_hmac_user->st_hmac_user_btcoex.st_hmac_btcoex_addba_req.en_ba_handle_allow)
    {
        if(BTCOEX_BLACKLIST_TPYE_FIX_BASIZE == HMAC_BTCOEX_GET_BLACKLIST_TYPE(pst_hmac_user))
        {
            OAM_WARNING_LOG0(0, OAM_SF_COEX, "{hmac_btcoex_delba_from_user::DO NOT DELBA.}");
        }
        else
        {
            OAM_WARNING_LOG0(0, OAM_SF_COEX, "{hmac_btcoex_delba_from_user::need to reassoc to READDBA.}");

            /* 发起reassoc req */
            //hmac_roam_trigger_handle(pst_hmac_vap, -122, OAL_TRUE);
            hmac_roam_start(pst_hmac_vap, ROAM_SCAN_CHANNEL_ORG_0, OAL_TRUE);

            /* 重关联之后，刷新为允许建立聚合 */
            pst_hmac_user->st_hmac_user_btcoex.st_hmac_btcoex_addba_req.en_ba_handle_allow = OAL_TRUE;

            /* 保证wifi恢复聚合64 */
            pst_hmac_user->st_hmac_user_btcoex.us_ba_size = 0;
        }

        return OAL_FAIL;
    }
    else if(OAL_TRUE == hmac_btcoex_check_exception_in_list(pst_hmac_vap, pst_hmac_user->st_user_base_info.auc_user_mac_addr))
    {
        /* 在黑名单之中，例如845N上行跑流，即使上行跑流没有进入黑名单，蓝牙结束仍然需要采用重关联，不然对端不回复arp rsp */
        if(WLAN_AMPDU_RX_BA_LUT_WSIZE == pst_hmac_user->st_hmac_user_btcoex.us_ba_size)
        {
            OAM_WARNING_LOG0(0, OAM_SF_COEX, "{hmac_btcoex_delba_from_user::blacklist user need to reassoc when bt is over.}");
            hmac_roam_start(pst_hmac_vap, ROAM_SCAN_CHANNEL_ORG_0, OAL_TRUE);
            return OAL_FAIL;
        }
    }

    return hmac_btcoex_delba_foreach_tid(pst_mac_vap, &(pst_hmac_user->st_user_base_info), &st_mac_cfg_delba_param);
}


oal_uint32 hmac_btcoex_rx_delba_trigger(mac_vap_stru *pst_mac_vap, oal_uint8 uc_len, oal_uint8 *puc_param)
{
    dmac_to_hmac_btcoex_rx_delba_trigger_event_stru *pst_dmac_to_hmac_btcoex_rx_delba;
    hmac_vap_stru                                   *pst_hmac_vap;
    hmac_user_stru                                  *pst_hmac_user;
    oal_uint32                                       ul_ret;

    pst_hmac_vap = (hmac_vap_stru *)mac_res_get_hmac_vap(pst_mac_vap->uc_vap_id);
    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_hmac_vap))
    {
        OAM_ERROR_LOG0(pst_mac_vap->uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_rx_delba_trigger::pst_hmac_vap is null!}");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_dmac_to_hmac_btcoex_rx_delba = (dmac_to_hmac_btcoex_rx_delba_trigger_event_stru *)puc_param;

    pst_hmac_user = mac_res_get_hmac_user(pst_dmac_to_hmac_btcoex_rx_delba->us_user_id);
    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_hmac_user))
    {
        OAM_WARNING_LOG1(pst_mac_vap->uc_vap_id, OAM_SF_COEX,
                        "{hmac_btcoex_rx_delba_trigger::pst_hmac_user is null! user_id is %d.}",
                        pst_dmac_to_hmac_btcoex_rx_delba->us_user_id);
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_hmac_user->st_hmac_user_btcoex.us_ba_size = (oal_uint16)pst_dmac_to_hmac_btcoex_rx_delba->uc_ba_size;

    if(OAL_TRUE == pst_dmac_to_hmac_btcoex_rx_delba->en_need_delba)
    {
        /* 刷新共存触发删建BA标记 */
        pst_hmac_user->st_hmac_user_btcoex.en_delba_btcoex_trigger  = OAL_TRUE;
    }

    OAM_WARNING_LOG3(pst_mac_vap->uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_rx_delba_trigger:delba size:%d, need_delba:%d, delba_trigger:%d.}",
        pst_dmac_to_hmac_btcoex_rx_delba->uc_ba_size,
        pst_dmac_to_hmac_btcoex_rx_delba->en_need_delba, pst_hmac_user->st_hmac_user_btcoex.en_delba_btcoex_trigger);

    if (pst_dmac_to_hmac_btcoex_rx_delba->en_need_delba)
    {
        ul_ret = hmac_btcoex_delba_from_user(pst_mac_vap, pst_hmac_user);
        if (OAL_SUCC != ul_ret)
        {
            OAM_WARNING_LOG1(pst_mac_vap->uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_rx_delba_trigger:delba send failed:ul_ret: %d.}", ul_ret);
            return ul_ret;
        }
    }

    return OAL_SUCC;
}


OAL_STATIC oal_uint32 hmac_btcoex_delba_send_timeout(oal_void *p_arg)
{
    hmac_btcoex_arp_req_process_stru *pst_hmac_btcoex_arp_req_process;
    hmac_user_btcoex_stru            *pst_hmac_user_btcoex;
    hmac_user_stru                   *pst_hmac_user;
    oal_uint32                        ui_val;

    pst_hmac_user = (hmac_user_stru *)p_arg;
    if (OAL_PTR_NULL == pst_hmac_user)
    {
        OAM_ERROR_LOG0(0, OAM_SF_COEX, "{hmac_btcoex_delba_send_timeout::pst_hmac_user is null.}");
        return OAL_ERR_CODE_PTR_NULL;
    }

    pst_hmac_user_btcoex = &(pst_hmac_user->st_hmac_user_btcoex);

    pst_hmac_btcoex_arp_req_process = &(pst_hmac_user_btcoex->st_hmac_btcoex_arp_req_process);

    ui_val = oal_atomic_read(&(pst_hmac_btcoex_arp_req_process->ul_rx_unicast_pkt_to_lan));
    if(0 == ui_val)
    {
        hmac_vap_stru *pst_hmac_vap;

        pst_hmac_vap = (hmac_vap_stru *)mac_res_get_hmac_vap(pst_hmac_user->st_user_base_info.uc_vap_id);
        if (OAL_UNLIKELY(OAL_PTR_NULL == pst_hmac_vap))
        {
            OAM_ERROR_LOG0(pst_hmac_user->st_user_base_info.uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_delba_send_timeout::pst_hmac_vap is null!}");
            return OAL_ERR_CODE_PTR_NULL;
        }

        pst_hmac_user_btcoex->uc_rx_no_pkt_count++;

        OAM_WARNING_LOG1(pst_hmac_user->st_user_base_info.uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_delba_send_timeout::rx_pkt[%d]!}",
            pst_hmac_user_btcoex->uc_rx_no_pkt_count);

        if(pst_hmac_user_btcoex->uc_rx_no_pkt_count > BTCOEX_ARP_FAIL_REASSOC_NUM)
        {
#if 0       /* 重关联逻辑暂时关闭，等统计出现哪些场景出现不通，才限制场景放开 */
            OAM_WARNING_LOG0(pst_hmac_user->st_user_base_info.uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_delba_send_timeout::need to reassoc to resume.}");

            /* 发起reassoc req */
            hmac_roam_start(pst_hmac_vap, ROAM_SCAN_CHANNEL_ORG_0, OAL_TRUE);
            /* 保证wifi恢复聚合64 */
            pst_hmac_user->st_hmac_user_btcoex.us_ba_size = 0;
#endif
        }
        else if(pst_hmac_user_btcoex->uc_rx_no_pkt_count > BTCOEX_ARP_FAIL_DELBA_NUM)
        {
            hmac_btcoex_delba_from_user(&pst_hmac_vap->st_vap_base_info, pst_hmac_user);
        }
    }
    else
    {
        pst_hmac_user_btcoex->uc_rx_no_pkt_count = 0;
    }

    oal_atomic_set(&pst_hmac_btcoex_arp_req_process->ul_rx_unicast_pkt_to_lan, 0);

    return OAL_SUCC;
}


oal_void hmac_btcoex_arp_fail_delba_process(oal_netbuf_stru *pst_netbuf, mac_vap_stru *pst_mac_vap)
{
    hmac_btcoex_arp_req_process_stru *pst_hmac_btcoex_arp_req_process;
    hmac_user_btcoex_stru            *pst_hmac_user_btcoex;
    mac_ether_header_stru            *pst_mac_ether_hdr;
    hmac_user_stru                   *pst_hmac_user;
    oal_uint8                         uc_data_type;

    if (OAL_TRUE != MAC_BTCOEX_CHECK_VALID_STA(pst_mac_vap))
    {
        return;
    }

    pst_mac_ether_hdr = (mac_ether_header_stru *)oal_netbuf_data(pst_netbuf);

    pst_hmac_user = mac_res_get_hmac_user(pst_mac_vap->us_assoc_vap_id);
    if (OAL_UNLIKELY(OAL_PTR_NULL == pst_hmac_user))
    {
        OAM_WARNING_LOG1(pst_mac_vap->uc_vap_id, OAM_SF_COEX,
                    "{hmac_btcoex_arp_fail_delba_process::pst_hmac_user is null!assoc_vap is %d.}",
                    pst_mac_vap->us_assoc_vap_id);
        return;
    }

    pst_hmac_user_btcoex = &(pst_hmac_user->st_hmac_user_btcoex);

    if((pst_hmac_user_btcoex->us_ba_size > 0) && (pst_hmac_user_btcoex->us_ba_size < WLAN_AMPDU_RX_BA_LUT_WSIZE))
    {
        /* 参数外面已经做检查，里面没必要再做检查了 */
        uc_data_type =  mac_get_data_type_from_8023((oal_uint8 *)pst_mac_ether_hdr, MAC_NETBUFF_PAYLOAD_ETH);

        pst_hmac_btcoex_arp_req_process = &(pst_hmac_user_btcoex->st_hmac_btcoex_arp_req_process);

        /* 发送方向创建定时器 */
        if((MAC_DATA_ARP_REQ == uc_data_type) && (OAL_FALSE == pst_hmac_btcoex_arp_req_process->st_delba_opt_timer.en_is_registerd))
        {
            /* 每次重启定时器之前清零,保证统计的时间 */
            oal_atomic_set(&(pst_hmac_btcoex_arp_req_process->ul_rx_unicast_pkt_to_lan), 0);

            FRW_TIMER_CREATE_TIMER(&(pst_hmac_btcoex_arp_req_process->st_delba_opt_timer),
                       hmac_btcoex_delba_send_timeout,
                       300,
                       pst_hmac_user,
                       OAL_FALSE,
                       OAM_MODULE_ID_HMAC,
                       pst_mac_vap->ul_core_id);
        }
    }
}


oal_void hmac_btcoex_blacklist_handle_init(oal_void *p_arg)
{
    hmac_user_stru *pst_hmac_user = (hmac_user_stru *)p_arg;

    /* 03采用方案2,02采用方案1 */
#if (_PRE_PRODUCT_ID == _PRE_PRODUCT_ID_HI1103_HOST)
    pst_hmac_user->st_hmac_user_btcoex.st_hmac_btcoex_addba_req.en_blacklist_tpye = BTCOEX_BLACKLIST_TPYE_NOT_AGGR;
#else
    pst_hmac_user->st_hmac_user_btcoex.st_hmac_btcoex_addba_req.en_blacklist_tpye = BTCOEX_BLACKLIST_TPYE_FIX_BASIZE;
#endif

    OAM_WARNING_LOG1(pst_hmac_user->st_user_base_info.uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_blacklist_handle_init::en_blacklist_tpye[%d]!}",
        pst_hmac_user->st_hmac_user_btcoex.st_hmac_btcoex_addba_req.en_blacklist_tpye);
}


oal_uint32 hmac_btcoex_check_exception_in_list(oal_void *p_arg, oal_uint8 *auc_addr)
{
    hmac_vap_stru                    *pst_hmac_vap = (hmac_vap_stru *)p_arg;
    hmac_btcoex_delba_exception_stru *pst_btcoex_exception;
    hmac_device_stru                 *pst_hmac_device;
    oal_uint8                         uc_index;

    pst_hmac_device = hmac_res_get_mac_dev(pst_hmac_vap->st_vap_base_info.uc_device_id);
    if (OAL_PTR_NULL == pst_hmac_device)
    {
        OAM_ERROR_LOG0(pst_hmac_vap->st_vap_base_info.uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_check_exception_in_list::pst_hmac_device null}");
        return OAL_FALSE;
    }

    for (uc_index = 0; uc_index < BTCOEX_BSS_NUM_IN_BLACKLIST; uc_index++)
    {
        pst_btcoex_exception = &(pst_hmac_device->st_hmac_device_btcoex.ast_hmac_btcoex_delba_exception[uc_index]);

        if ((pst_btcoex_exception->uc_used != 0)
            && (0 == oal_compare_mac_addr(pst_btcoex_exception->auc_user_mac_addr, auc_addr)))
        {
            OAM_WARNING_LOG4(0, OAM_SF_COEX,
                "{hmac_btcoex_check_exception_in_list::Find in blacklist, addr->%02x:%02x:XX:XX:%02x:%02x.}",
                auc_addr[0], auc_addr[1], auc_addr[4], auc_addr[5]);
            return OAL_TRUE;
        }
    }

    return OAL_FALSE;
}


OAL_STATIC oal_void hmac_btcoex_add_exception_to_list(hmac_vap_stru *pst_hmac_vap, oal_uint8 *auc_mac_addr)
{
    hmac_btcoex_delba_exception_stru *pst_btcoex_exception;
    hmac_device_btcoex_stru          *pst_hmac_device_btcoex;
    hmac_device_stru                 *pst_hmac_device;

    pst_hmac_device = hmac_res_get_mac_dev(pst_hmac_vap->st_vap_base_info.uc_device_id);
    if (OAL_PTR_NULL == pst_hmac_device)
    {
        OAM_ERROR_LOG0(pst_hmac_vap->st_vap_base_info.uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_check_exception_in_list::pst_hmac_device null}");
        return;
    }

    pst_hmac_device_btcoex = &(pst_hmac_device->st_hmac_device_btcoex);

    if (pst_hmac_device_btcoex->uc_exception_bss_index >= BTCOEX_BSS_NUM_IN_BLACKLIST)
    {
        OAM_WARNING_LOG1(pst_hmac_vap->st_vap_base_info.uc_vap_id, OAM_SF_COEX, "{hmac_btcoex_add_exception_to_list::already reach max num:%d.}", BTCOEX_BSS_NUM_IN_BLACKLIST);
        pst_hmac_device_btcoex->uc_exception_bss_index = 0;
    }
    pst_btcoex_exception = &(pst_hmac_device_btcoex->ast_hmac_btcoex_delba_exception[pst_hmac_device_btcoex->uc_exception_bss_index]);
    oal_set_mac_addr(pst_btcoex_exception->auc_user_mac_addr, auc_mac_addr);
    pst_btcoex_exception->uc_type = 0;
    pst_btcoex_exception->uc_used = 1;

    pst_hmac_device_btcoex->uc_exception_bss_index++;
}


oal_void hmac_btcoex_check_rx_same_baw_start_from_addba_req(oal_void *p_arg,
                                                                                    oal_void *p_arg1,
                                                                                    mac_ieee80211_frame_stru *pst_frame_hdr,
                                                                                    oal_uint8 *puc_action)
{
    hmac_vap_stru                  *pst_hmac_vap = (hmac_vap_stru *)p_arg;
    hmac_user_stru                 *pst_hmac_user = (hmac_user_stru *)p_arg1;
    hmac_btcoex_addba_req_stru     *pst_hmac_btcoex_addba_req;
    hmac_user_btcoex_stru          *pst_hmac_user_btcoex;
    oal_uint16                      us_baw_start;
    oal_uint8                       uc_tid;

    pst_hmac_user_btcoex = &(pst_hmac_user->st_hmac_user_btcoex);

    pst_hmac_btcoex_addba_req = &(pst_hmac_user_btcoex->st_hmac_btcoex_addba_req);

    /* 两次收到addba req的start num一样且不是重传帧，认为对端移窗卡死  */
    if (OAL_TRUE == pst_frame_hdr->st_frame_control.bit_retry
        && pst_frame_hdr->bit_seq_num == pst_hmac_btcoex_addba_req->us_last_seq_num)
    {
        OAM_WARNING_LOG0(0, OAM_SF_COEX,
                "{hmac_btcoex_check_rx_same_baw_start_from_addba_req::retry addba req.}");
        return;
    }

    /******************************************************************/
    /*       ADDBA Request Frame - Frame Body                         */
    /* ---------------------------------------------------------------*/
    /* | Category | Action | Dialog | Parameters | Timeout | SSN     |*/
    /* ---------------------------------------------------------------*/
    /* | 1        | 1      | 1      | 2          | 2       | 2       |*/
    /* ---------------------------------------------------------------*/
    /*                                                                */
    /******************************************************************/
    us_baw_start = (puc_action[7] >> 4) | (puc_action[8] << 4);
    uc_tid = (puc_action[3] & 0x3C) >> 2;
    if (uc_tid != 0)
    {
        OAM_WARNING_LOG0(0, OAM_SF_COEX,
                "{hmac_btcoex_check_rx_same_baw_start_from_addba_req::uc_tid != 0.}");
        return;
    }

    if ((us_baw_start != 0)
        && (us_baw_start == pst_hmac_btcoex_addba_req->us_last_baw_start)
        && (pst_hmac_user_btcoex->uc_rx_no_pkt_count > 2))
    {
        OAM_WARNING_LOG1(0, OAM_SF_COEX,
                    "{hmac_btcoex_check_rx_same_baw_start_from_addba_req::baw_start:%d, delba will forbidden.}", us_baw_start);

        /* 黑名单ba处理标记 */
        pst_hmac_btcoex_addba_req->en_ba_handle_allow = OAL_FALSE;

        if (OAL_FALSE == hmac_btcoex_check_exception_in_list(pst_hmac_vap, pst_frame_hdr->auc_address2))
        {
            OAM_WARNING_LOG0(0, OAM_SF_COEX,
                    "{hmac_btcoex_check_rx_same_baw_start_from_addba_req::write down to file.}");
            hmac_btcoex_add_exception_to_list(pst_hmac_vap, pst_frame_hdr->auc_address2);
        }

        if(BTCOEX_BLACKLIST_TPYE_FIX_BASIZE == HMAC_BTCOEX_GET_BLACKLIST_TYPE(pst_hmac_user))
        {
            /* 发送去认证帧到AP */
            hmac_mgmt_send_disassoc_frame(&pst_hmac_vap->st_vap_base_info,
                                        pst_frame_hdr->auc_address2,
                                        MAC_UNSPEC_REASON,
                                        pst_hmac_user->st_user_base_info.st_cap_info.bit_pmf_active);

            /* 删除对应用户 */
            hmac_user_del(&pst_hmac_vap->st_vap_base_info, pst_hmac_user);

            hmac_sta_handle_disassoc_rsp(pst_hmac_vap, MAC_AUTH_NOT_VALID);
        }
    }

    pst_hmac_btcoex_addba_req->us_last_baw_start = us_baw_start;
    pst_hmac_btcoex_addba_req->us_last_seq_num = pst_frame_hdr->bit_seq_num;
}


oal_uint32  hmac_config_print_btcoex_status(mac_vap_stru *pst_mac_vap, oal_uint16 us_len, oal_uint8 *puc_param)
{
    hmac_btcoex_delba_exception_stru *pst_btcoex_exception;
    hmac_device_stru                 *pst_hmac_device;
    oal_uint32                        ul_ret;
    oal_uint8                         uc_index;

    pst_hmac_device = hmac_res_get_mac_dev(pst_mac_vap->uc_device_id);
    if (OAL_PTR_NULL == pst_hmac_device)
    {
        OAM_ERROR_LOG0(pst_mac_vap->uc_vap_id, OAM_SF_COEX, "{hmac_config_print_btcoex_status::pst_hmac_device null}");
        return OAL_ERR_CODE_PTR_NULL;
    }

    /***************************************************************************
        抛事件到DMAC层, 同步DMAC数据
    ***************************************************************************/
    ul_ret = hmac_config_send_event(pst_mac_vap, WLAN_CFGID_BTCOEX_STATUS_PRINT, us_len, puc_param);
    if (OAL_UNLIKELY(OAL_SUCC != ul_ret))
    {
        OAM_WARNING_LOG1(pst_mac_vap->uc_vap_id, OAM_SF_COEX,
                        "{hmac_config_print_btcoex_status::send event return err code [%d].}", ul_ret);
    }

    for (uc_index = 0; uc_index < BTCOEX_BSS_NUM_IN_BLACKLIST; uc_index++)
    {
        pst_btcoex_exception = &(pst_hmac_device->st_hmac_device_btcoex.ast_hmac_btcoex_delba_exception[uc_index]);
        if (pst_btcoex_exception->uc_used != 0)
        {
            OAM_WARNING_LOG4(0, OAM_SF_COEX,
                    "{hmac_config_print_btcoex_status::addr->%02x:%02x:XX:XX:%02x:%02x.}",
                    pst_btcoex_exception->auc_user_mac_addr[0],
                    pst_btcoex_exception->auc_user_mac_addr[1],
                    pst_btcoex_exception->auc_user_mac_addr[4],
                    pst_btcoex_exception->auc_user_mac_addr[5]);
        }
    }

    return ul_ret;
}


oal_uint32  hmac_config_btcoex_preempt_tpye(mac_vap_stru *pst_mac_vap, oal_uint16 us_len, oal_uint8 *puc_param)
{
    oal_uint32 ul_ret;

    /***************************************************************************
        抛事件到DMAC层, 同步DMAC数据
    ***************************************************************************/
    ul_ret = hmac_config_send_event(pst_mac_vap, WLAN_CFGID_BTCOEX_PREEMPT_TYPE, us_len, puc_param);
    if (OAL_UNLIKELY(OAL_SUCC != ul_ret))
    {
        OAM_WARNING_LOG1(pst_mac_vap->uc_vap_id, OAM_SF_COEX,
                        "{hmac_config_btcoex_preempt_tpye::send event return err code [%d].}", ul_ret);
    }

    return ul_ret;
}


oal_uint32  hmac_config_btcoex_set_perf_param(mac_vap_stru *pst_mac_vap, oal_uint16 us_len, oal_uint8 *puc_param)
{
    oal_uint32 ul_ret;

    /***************************************************************************
        抛事件到DMAC层, 同步DMAC数据
    ***************************************************************************/
    ul_ret = hmac_config_send_event(pst_mac_vap, WLAN_CFGID_BTCOEX_SET_PERF_PARAM, us_len, puc_param);
    if (OAL_UNLIKELY(OAL_SUCC != ul_ret))
    {
        OAM_WARNING_LOG1(pst_mac_vap->uc_vap_id, OAM_SF_COEX,
                        "{hmac_config_btcoex_set_perf_param::send event return err code [%d].}", ul_ret);
    }

    return ul_ret;
}


oal_uint32 hmac_btcoex_check_by_ba_size(hmac_user_stru *pst_hmac_user)
{
    hmac_user_btcoex_stru *pst_hmac_user_btcoex;

    pst_hmac_user_btcoex = &(pst_hmac_user->st_hmac_user_btcoex);

    if ((pst_hmac_user_btcoex->us_ba_size > 0) && (pst_hmac_user_btcoex->us_ba_size < WLAN_AMPDU_RX_BA_LUT_WSIZE))
    {
        return OAL_TRUE;
    }

    return OAL_FALSE;
}
#endif /* #ifdef _PRE_WLAN_FEATURE_COEXIST_BT */

#ifdef __cplusplus
    #if __cplusplus
        }
    #endif
#endif

#endif /* end of __HMAC_BTCOEX_C__ */

