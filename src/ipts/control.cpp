// SPDX-License-Identifier: GPL-2.0-or-later

#include "control.hpp"

#include "ipts.h"

#include <common/cerror.hpp>
#include <common/types.hpp>

#include <cstddef>
#include <fcntl.h>
#include <span>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

IptsControl::IptsControl() : files()
{
	for (int i = 0; i < IPTS_BUFFERS; i++) {
		std::string name("/dev/ipts/" + std::to_string(i));

		int ret = open(name.c_str(), O_RDONLY);
		if (ret == -1)
			throw iptsd::common::cerror("Failed to open " + name);

		this->files.at(i) = ret;
	}

	this->flush();
	this->get_device_info();
	this->current_doorbell = this->doorbell();
}

IptsControl::~IptsControl()
{
	for (int i = 0; i < IPTS_BUFFERS; i++)
		close(this->files.at(i));
}

int IptsControl::current()
{
	return this->files.at(this->current_doorbell % IPTS_BUFFERS);
}

bool IptsControl::ready()
{
	u8 ready = 0;

	int ret = ioctl(this->current(), IPTS_IOCTL_GET_DEVICE_READY, &ready);
	if (ret == -1)
		return false;

	return ready > 0;
}

void IptsControl::wait_for_device()
{
	for (int i = 0; i < 5; i++) {
		if (this->ready())
			break;

		sleep(1);
	}
}

void IptsControl::get_device_info()
{
	this->wait_for_device();

	int ret = ioctl(this->current(), IPTS_IOCTL_GET_DEVICE_INFO, &this->info);
	if (ret == -1)
		throw iptsd::common::cerror("Failed to get device info");
}

void IptsControl::send_feedback(int file)
{
	this->wait_for_device();

	int ret = ioctl(file, IPTS_IOCTL_SEND_FEEDBACK, nullptr);
	if (ret == -1)
		throw iptsd::common::cerror("Failed to send feedback");
}

void IptsControl::send_feedback()
{
	int file = this->current();
	this->send_feedback(file);

	this->current_doorbell++;
}

void IptsControl::flush()
{
	for (int i = 0; i < IPTS_BUFFERS; i++)
		this->send_feedback(this->files.at(i));
}

u32 IptsControl::doorbell()
{
	this->wait_for_device();

	u32 doorbell = 0;

	int ret = ioctl(this->current(), IPTS_IOCTL_GET_DOORBELL, &doorbell);
	if (ret == -1)
		throw iptsd::common::cerror("Failed to get doorbell");

	/*
	 * If the new doorbell is lower than the value we have stored,
	 * the device has been reset below our feet (i.e. through suspending).
	 *
	 * We send feedback to clear all buffers and reset the stored value.
	 */
	if (this->current_doorbell > doorbell) {
		this->flush();
		this->current_doorbell = doorbell;
	}

	return doorbell;
}

ssize_t IptsControl::read(std::span<u8> dest)
{
	this->wait_for_device();

	ssize_t ret = ::read(this->current(), dest.data(), dest.size());
	if (ret == -1)
		throw iptsd::common::cerror("Failed to read from buffer");

	return ret;
}

void IptsControl::reset()
{
	this->wait_for_device();

	int ret = ioctl(this->current(), IPTS_IOCTL_SEND_RESET, nullptr);
	if (ret == -1)
		throw iptsd::common::cerror("Failed to reset IPTS");
}
