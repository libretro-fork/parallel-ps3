#pragma once

#include "Emu/Io/MouseHandler.h"

class NullMouseHandler final : public MouseHandlerBase
{
	using MouseHandlerBase::MouseHandlerBase;

public:
	void Init(const u32 max_connect) override
	{
		set_ports(max_connect, 0, true, nullptr);
	}
};
