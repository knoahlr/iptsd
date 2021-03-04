/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _IPTSD_IPTS_CONTROL_HPP_
#define _IPTSD_IPTS_CONTROL_HPP_

#include "ipts.h"

#include <common/types.hpp>

#include <array>
#include <cstddef>
#include <span>

class IptsControl {
public:
	struct ipts_device_info info {};
	u32 current_doorbell = 0;

	IptsControl();
	~IptsControl();

	void send_feedback();
	u32 doorbell();
	ssize_t read(std::span<u8> dest);
	void reset();

private:
	std::array<int, IPTS_BUFFERS> files;

	int current();
	bool ready();
	void wait_for_device();
	void get_device_info();
	void send_feedback(int file);
	void flush();
};

#endif /* _IPTSD_IPTS_CONTROL_HPP_ */
