#include "stdafx.h"
#include "Emu/IdManager.h"
#include "Emu/Cell/PPUModule.h"
#include "Utilities/Thread.h"

#include "Emu/Cell/lv2/sys_spu.h"

#include "util/asm.hpp"

// SONIC THE HEDGEDOG: a fix for a race condition between SPUs and PPUs causing missing graphics (SNR is overriden when non-empty)
void WaitForSPUsToEmptySNRs(ppu_thread& ppu, u32 spu_id, u32 snr_mask)
{
	ppu.state += cpu_flag::wait;

	auto [spu, group] = lv2_spu_group::get_thread(spu_id);

	if ((!spu && spu_id != umax) || snr_mask % 4 == 0)
	{
		return;
	}

	// Wait until specified SNRs are reported empty at least once
	// Between looks: until the first busy SNR's word changes (the SPU's read of it is a store the line watch sees:
	// UMWAIT, MWAITX or WFE), else a timed wait of 100 us; where it yielded the host's thread
	const retro_atomic_64_t* busy_word = nullptr;
	u64 busy_seen = 0;

	for (bool has_busy = true; has_busy && !ppu.is_stopped();)
	{
		if (busy_word)
		{
			utils::spin_on_cacheline_once(*static_cast<const u64*>(static_cast<const void*>(busy_word)), busy_seen, 100);
		}

		has_busy = false;
		busy_word = nullptr;

		auto for_one = [&](u32, spu_thread& spu)
		{
			if (has_busy)
			{
				return;
			}

			for (auto* ch : {(snr_mask & 1) ? &spu.ch_snr1 : nullptr, (snr_mask & 2) ? &spu.ch_snr2 : nullptr})
			{
				if (!ch)
				{
					continue;
				}

				const u64 seen = ch->load();

				if (ch->get_count())
				{
					has_busy = true;
					busy_word = &ch->data;
					busy_seen = seen;
					return;
				}
			}
		};

		if (spu)
		{
			// Wait for a single SPU
			for_one(spu->id, *spu);
		}
		else
		{
			// Wait for all SPUs
			idm::select<named_thread<spu_thread>>(for_one);
		}
	}
}

DECLARE(ppu_module_manager::hle_patches)("RPCS3_HLE_LIBRARY", []()
{
	REG_FUNC(RPCS3_HLE_LIBRARY, WaitForSPUsToEmptySNRs);
});
