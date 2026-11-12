// cellVdec's decoded frames (Emu/Cell/Modules/cellVdec.cpp): plain data in
// two retro_deques made with the decoder, where there were std::deques of
// frames that each owned their picture through a shared_ptr to the pool.
// A picture's buffer now goes back to the pool by hand, so:
//
// - a frame queued and taken as cellVdecGetPictureExt takes it comes out
//   in order, with the picture it went in with;
// - every way a frame leaves a queue gives its buffer back: taken by the
//   game, flushed by a new sequence or a close, left by an aborted AU, or
//   still queued when the decoder is destroyed - no buffer is lost, and
//   none is given back twice;
// - the queues are the size they were made, after thousands of pictures.
//
// cellVdec.cpp is compiled into this test (the decoder's context is
// private to it); the rest is the core's own objects and archives, the
// same link as the core's with this main. Built by `make check`.

#include "../../Emu/Cell/Modules/cellVdec.cpp"

#include <cstdio>
#include <cstdlib>
#include <set>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// Buffers resting in the pool
static usz pooled(const vdec_pic_pool& pool)
{
	usz count = 0;
	for (const auto& slot : pool.slots)
		count += slot.load() != nullptr;
	return count;
}

static vdec_frame frame_of(vdec_context& vdec, u64 n)
{
	vdec_frame frame;
	frame.seq_id = 1;
	frame.cmd_id = n;
	frame.pts = n * 3003;
	frame.pic = vdec.pic_pool.take();
	return frame;
}

int main()
{
	{
		vdec_context vdec(CELL_VDEC_CODEC_TYPE_AVC, 0, 0, 0, vm::null, 0);
		CHECK(vdec_frames_limit(&vdec.out_queue) == 64 && pooled(vdec.pic_pool) == 0);

		// Decoded, queued for the game, taken by it: in order, and the
		// buffers end up in the pool - as many as were ever in flight
		std::set<vdc_pic*> seen;
		u64 next_in = 0, next_out = 0, wrong = 0;
		for (u32 step = 0; step < 5000; step++)
		{
			// An AU's pictures
			const u32 pictures = 1 + step % 3;
			for (u32 i = 0; i < pictures; i++)
			{
				const vdec_frame frame = frame_of(vdec, next_in++);
				seen.insert(frame.pic);
				CHECK(vdec_frames_push_back_growing(&vdec.decoded, &frame));
			}
			// On to the output queue while there is room
			while (!vdec_frames_empty(&vdec.decoded) && vdec_frames_count(&vdec.out_queue) <= vdec.out_max)
			{
				CHECK(vdec_frames_push_back_growing(&vdec.out_queue, vdec_frames_front(&vdec.decoded)));
				vdec_frames_pop_front(&vdec.decoded);
			}
			// The game takes some
			for (u32 i = 0; i < 1 + step % 4 && !vdec_frames_empty(&vdec.out_queue); i++)
			{
				vdec_frame frame = *vdec_frames_front(&vdec.out_queue);
				vdec_frames_pop_front(&vdec.out_queue);
				wrong += frame.cmd_id != next_out || frame.pts != next_out * 3003 || !frame.pic;
				next_out++;
				vdec.give_back(frame);
				wrong += frame.pic != nullptr;
			}
		}
		CHECK(wrong == 0 && next_out > 4000);
		CHECK(vdec_frames_limit(&vdec.out_queue) == 64);
		CHECK(seen.size() <= vdec_pic_pool::size);

		const usz in_flight = vdec_frames_count(&vdec.out_queue) + vdec_frames_count(&vdec.decoded);
		CHECK(pooled(vdec.pic_pool) + in_flight == seen.size());

		// A new sequence, or a close, flushes the output queue; an aborted
		// AU leaves pictures behind: all their buffers go back
		for (u64 n = 0; n < 10; n++)
		{
			const vdec_frame frame = frame_of(vdec, n);
			seen.insert(frame.pic);
			CHECK(vdec_frames_push_back_growing(n < 7 ? &vdec.out_queue : &vdec.decoded, &frame));
		}
		CHECK(vdec_frames_count(&vdec.out_queue) >= 7 && vdec_frames_count(&vdec.decoded) >= 3);
		CHECK(pooled(vdec.pic_pool) + 10 <= seen.size());
		vdec.flush(vdec.out_queue);
		vdec.flush(vdec.decoded);
		CHECK(vdec_frames_empty(&vdec.out_queue) && vdec_frames_empty(&vdec.decoded));
		CHECK(pooled(vdec.pic_pool) == seen.size());

		// Taken again, they are the same buffers: none was freed or lost
		std::vector<vdec_frame> again;
		for (usz i = 0; i < seen.size(); i++)
		{
			again.push_back(frame_of(vdec, i));
			CHECK(seen.count(again.back().pic) == 1);
		}
		CHECK(pooled(vdec.pic_pool) == 0);

		// Given back once: a second give of the same frame does nothing
		for (vdec_frame& frame : again)
		{
			vdec.give_back(frame);
			vdec.give_back(frame);
		}
		CHECK(pooled(vdec.pic_pool) == seen.size());

		// Left queued when the decoder goes: the destructor returns them
		// (a leak or a double free here is the sanitizer's to find)
		for (u64 n = 0; n < 10; n++)
		{
			const vdec_frame frame = frame_of(vdec, n);
			CHECK(vdec_frames_push_back_growing(n & 1 ? &vdec.out_queue : &vdec.decoded, &frame));
		}
	}

	std::printf("test_vdec_frames: %s\n", failures ? "FAIL" : "ok");
	std::fflush(stdout);
	std::_Exit(failures ? 1 : 0);
}
