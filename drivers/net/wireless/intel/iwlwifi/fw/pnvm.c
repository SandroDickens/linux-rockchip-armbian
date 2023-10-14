// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/******************************************************************************
 *
 * Copyright(c) 2020 Intel Corporation
 *
 *****************************************************************************/

#include "iwl-drv.h"
#include "pnvm.h"
#include "iwl-prph.h"
#include "iwl-io.h"
#include "fw/api/commands.h"
#include "fw/api/nvm-reg.h"
#include "fw/api/alive.h"

struct iwl_pnvm_section {
	__le32 offset;
	const u8 data[];
} __packed;

static bool iwl_pnvm_complete_fn(struct iwl_notif_wait_data *notif_wait,
				 struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_trans *trans = (struct iwl_trans *)data;
	struct iwl_pnvm_init_complete_ntfy *pnvm_ntf = (void *)pkt->data;

	IWL_DEBUG_FW(trans,
		     "PNVM complete notification received with status %d\n",
		     le32_to_cpu(pnvm_ntf->status));

	return true;
}

int iwl_pnvm_load(struct iwl_trans *trans,
		  struct iwl_notif_wait_data *notif_wait)
{
	struct iwl_notification_wait pnvm_wait;
	static const u16 ntf_cmds[] = { WIDE_ID(REGULATORY_AND_NVM_GROUP,
						PNVM_INIT_COMPLETE_NTFY) };
	int ret;

	/* if the SKU_ID is empty, there's nothing to do */
	if (!trans->sku_id[0] && !trans->sku_id[1] && !trans->sku_id[2])
		return 0;

	/* load from disk only if we haven't done it (or tried) before */
	if (!trans->pnvm_loaded) {
#if 1
		/* Older iwlwifi ucode firmware used in 5.10 doesn't expect any pnvm file,
		 * which contains FW configuration data for each different HW combination
		 * that can be used with newer firmware and kernel. So load nothing here
		 * to bypass the issue on AX210, where pnvm file was first introduced.
		 */
		trans->pnvm_loaded = true;
#else
		const struct firmware *pnvm;
		char pnvm_name[64];

		/*
		 * The prefix unfortunately includes a hyphen at the end, so
		 * don't add the dot here...
		 */
		snprintf(pnvm_name, sizeof(pnvm_name), "%spnvm",
			 trans->cfg->fw_name_pre);

		/* ...but replace the hyphen with the dot here. */
		if (strlen(trans->cfg->fw_name_pre) < sizeof(pnvm_name))
			pnvm_name[strlen(trans->cfg->fw_name_pre) - 1] = '.';

		ret = firmware_request_nowarn(&pnvm, pnvm_name, trans->dev);
		if (ret) {
			IWL_DEBUG_FW(trans, "PNVM file %s not found %d\n",
				     pnvm_name, ret);
			/*
			 * Pretend we've loaded it - at least we've tried and
			 * couldn't load it at all, so there's no point in
			 * trying again over and over.
			 */
			trans->pnvm_loaded = true;
		} else {
			iwl_pnvm_parse(trans, pnvm->data, pnvm->size);

			release_firmware(pnvm);
		}
#endif
	} else {
		/* if we already loaded, we need to set it again */
		ret = iwl_trans_set_pnvm(trans, NULL, 0);
		if (ret)
			return ret;
	}

	iwl_init_notification_wait(notif_wait, &pnvm_wait,
				   ntf_cmds, ARRAY_SIZE(ntf_cmds),
				   iwl_pnvm_complete_fn, trans);

	/* kick the doorbell */
	iwl_write_umac_prph(trans, UREG_DOORBELL_TO_ISR6,
			    UREG_DOORBELL_TO_ISR6_PNVM);

	return iwl_wait_notification(notif_wait, &pnvm_wait,
				     MVM_UCODE_PNVM_TIMEOUT);
}
IWL_EXPORT_SYMBOL(iwl_pnvm_load);
