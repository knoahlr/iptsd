// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef IPTSD_APPS_DAEMON_TOUCH_HPP
#define IPTSD_APPS_DAEMON_TOUCH_HPP

#include "uinput-device.hpp"

#include <common/types.hpp>
#include <contacts/contact.hpp>
#include <core/generic/config.hpp>
#include <core/generic/device.hpp>

#include <linux/input-event-codes.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace iptsd::apps::daemon {

class TouchDevice {
private:
	std::shared_ptr<UinputDevice> m_uinput = std::make_shared<UinputDevice>();

	// The daemon configuration.
	core::Config m_config;

	// The indices of the contacts in the current frame.
	std::set<usize> m_current {};

	// The indices of the contacts in the last frame.
	std::set<usize> m_last {};

	// The difference between m_last and m_current.
	std::set<usize> m_lift {};

	// The index of the contact that is emitted through the singletouch API.
	usize m_single_index = 0;

	// Whether the device is enabled.
	bool m_enabled = true;

public:
	TouchDevice(const core::Config &config, const core::DeviceInfo &info) : m_config {config}
	{
		m_uinput->set_name("IPTS Touch");
		m_uinput->set_vendor(info.vendor);
		m_uinput->set_product(info.product);

		m_uinput->set_evbit(EV_ABS);
		m_uinput->set_evbit(EV_KEY);

		m_uinput->set_propbit(INPUT_PROP_DIRECT);
		m_uinput->set_keybit(BTN_TOUCH);

		const f64 diag = std::hypot(config.width, config.height);

		// Resolution for X / Y is expected to be units/mm.
		const i32 res_x = gsl::narrow<i32>(std::round(IPTS_MAX_X / (config.width * 10)));
		const i32 res_y = gsl::narrow<i32>(std::round(IPTS_MAX_Y / (config.height * 10)));
		const i32 res_d = gsl::narrow<i32>(std::round(IPTS_DIAGONAL / (diag * 10)));

		m_uinput->set_absinfo(ABS_MT_SLOT, 0, IPTS_MAX_CONTACTS, 0);
		m_uinput->set_absinfo(ABS_MT_TRACKING_ID, 0, IPTS_MAX_CONTACTS, 0);
		m_uinput->set_absinfo(ABS_MT_POSITION_X, 0, IPTS_MAX_X, res_x);
		m_uinput->set_absinfo(ABS_MT_POSITION_Y, 0, IPTS_MAX_Y, res_y);
		m_uinput->set_absinfo(ABS_MT_ORIENTATION, 0, 180, 0);
		m_uinput->set_absinfo(ABS_MT_TOUCH_MAJOR, 0, IPTS_DIAGONAL, res_d);
		m_uinput->set_absinfo(ABS_MT_TOUCH_MINOR, 0, IPTS_DIAGONAL, res_d);
		m_uinput->set_absinfo(ABS_X, 0, IPTS_MAX_X, res_x);
		m_uinput->set_absinfo(ABS_Y, 0, IPTS_MAX_Y, res_y);

		m_uinput->create();
	}

	/*!
	 * Passes a frame of detected contacts to the linux kernel.
	 *
	 * @param[in] contacts All currently active contacts.
	 */
	void update(const std::vector<contacts::Contact<f64>> &contacts)
	{
		// If the touchscreen is disabled ignore all inputs.
		if (!m_enabled)
			return;

		// Find the inputs that need to be lifted
		this->search_lifted(contacts);

		if (this->is_blocked(contacts)) {
			this->lift_all();
		} else {
			this->process_multitouch(contacts);
			this->process_singletouch(contacts);
		}

		this->sync();
	}

	/*!
	 * Disables the touchscreen and lifts all contacts.
	 */
	void disable()
	{
		m_enabled = false;

		// Lift all currently active contacts.
		this->lift_all();
		this->sync();

		m_current.clear();
		m_last.clear();
		m_lift.clear();
	}

	/*!
	 * Enables the touchscreen.
	 */
	void enable()
	{
		m_enabled = true;
	}

	/*!
	 * Whether the touchscreen is disabled or enabled.
	 *
	 * @return true if the touchscreen is enabled.
	 */
	[[nodiscard]] bool enabled() const
	{
		return m_enabled;
	}

	/*!
	 * Whether the touchscreen is currently active.
	 *
	 * @return true if there are any active inputs.
	 */
	[[nodiscard]] bool active() const
	{
		return !m_current.empty();
	}

private:
	/*!
	 * Builds the difference between the current and the last frame.
	 * Contacts that were present in the last frame but not in this one have to be lifted.
	 *
	 * @param[in] contacts All currently active contacts.
	 */
	void search_lifted(const std::vector<contacts::Contact<f64>> &contacts)
	{
		std::swap(m_current, m_last);

		m_current.clear();

		// Build a set of current indices
		for (const contacts::Contact<f64> &contact : contacts) {
			if (!contact.index.has_value())
				continue;

			m_current.insert(contact.index.value());
		}

		m_lift.clear();

		// Determine all indices that were in the last frame but not in this one
		std::set_difference(m_last.cbegin(), m_last.cend(), m_current.cbegin(),
				    m_current.cend(), std::inserter(m_lift, m_lift.begin()));
	}

	/*!
	 * Checks if the touchscreen should be disabled because of a palm on the screen.
	 *
	 * @param[in] contacts All currently active contacts.
	 * @return true if all contacts should be lifted.
	 */
	[[nodiscard]] bool is_blocked(const std::vector<contacts::Contact<f64>> &contacts) const
	{
		if (!m_config.touch_disable_on_palm)
			return false;

		return std::any_of(contacts.cbegin(), contacts.cend(),
				   [&](const auto &c) { return !c.valid.value_or(true); });
	}

	/*!
	 * Emits linux multitouch events for every contact.
	 *
	 * @param[in] contacts All currently active contacts.
	 */
	void process_multitouch(const std::vector<contacts::Contact<f64>> &contacts) const
	{
		for (const contacts::Contact<f64> &contact : contacts) {
			// Ignore contacts without an index
			if (!contact.index.has_value())
				continue;

			// Ignore unstable changes
			if (!contact.stable.value_or(true))
				continue;

			if (contact.valid.value_or(true))
				this->emit_multitouch(contact);
			else
				this->lift_multitouch(contact.index.value_or(0));
		}

		for (const usize &index : m_lift)
			this->lift_multitouch(index);
	}

	/*!
	 * Emits a lift event using the linux multitouch protocol.
	 */
	void lift_multitouch(const usize index) const
	{
		m_uinput->emit(EV_ABS, ABS_MT_SLOT, gsl::narrow<i32>(index));
		m_uinput->emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
	}

	/*!
	 * Emits a contact event using the linux multitouch protocol.
	 *
	 * @param[in] contact The contact to emit.
	 */
	void emit_multitouch(const contacts::Contact<f64> &contact) const
	{
		const Vector2<f64> size = contact.size;

		Vector2<f64> mean = contact.mean;
		f64 orientation = contact.orientation;

		if (m_config.invert_x)
			mean.x() = 1.0 - mean.x();

		if (m_config.invert_y)
			mean.y() = 1.0 - mean.y();

		if (m_config.invert_x != m_config.invert_y)
			orientation = 1.0 - orientation;

		const i32 index = gsl::narrow<i32>(contact.index.value_or(0));

		const i32 x = gsl::narrow<i32>(std::round(mean.x() * IPTS_MAX_X));
		const i32 y = gsl::narrow<i32>(std::round(mean.y() * IPTS_MAX_Y));

		const i32 angle = gsl::narrow<i32>(std::round(orientation * 180));
		const i32 major = gsl::narrow<i32>(std::round(size.maxCoeff() * IPTS_DIAGONAL));
		const i32 minor = gsl::narrow<i32>(std::round(size.minCoeff() * IPTS_DIAGONAL));

		m_uinput->emit(EV_ABS, ABS_MT_SLOT, index);
		m_uinput->emit(EV_ABS, ABS_MT_TRACKING_ID, index);
		m_uinput->emit(EV_ABS, ABS_MT_POSITION_X, x);
		m_uinput->emit(EV_ABS, ABS_MT_POSITION_Y, y);

		m_uinput->emit(EV_ABS, ABS_MT_ORIENTATION, angle);
		m_uinput->emit(EV_ABS, ABS_MT_TOUCH_MAJOR, major);
		m_uinput->emit(EV_ABS, ABS_MT_TOUCH_MINOR, minor);
	}

	/*!
	 * Selects a single contact and emits a linux singletouch event.
	 *
	 * @param[in] contacts All currently active contacts.
	 */
	void process_singletouch(const std::vector<contacts::Contact<f64>> &contacts)
	{
		const bool reset = m_lift.find(m_single_index) == m_lift.cend();

		if (!reset) {
			for (const contacts::Contact<f64> &contact : contacts) {
				if (contact.index != m_single_index)
					continue;

				// If the contact should be lifted select a new one.
				if (!m_enabled || !contact.valid.value_or(true))
					break;

				// Ignore unstable changes
				if (!contact.stable.value_or(true))
					return;

				this->emit_singletouch(contact);
				return;
			}
		}

		this->lift_singletouch();

		if (!m_enabled)
			return;

		// If this loop is reached the contact was lifted and a new one has to be found.
		for (const contacts::Contact<f64> &contact : contacts) {
			if (contact.index == m_single_index)
				continue;

			if (!contact.valid.value_or(true))
				continue;

			m_single_index = contact.index.value_or(0);
			return;
		}
	}

	/*!
	 * Emits a lift event using the linux singletouch protocol.
	 */
	void lift_singletouch() const
	{
		m_uinput->emit(EV_KEY, BTN_TOUCH, 0);
	}

	/*!
	 * Emits a contact event using the linux singletouch protocol.
	 *
	 * @param[in] contact The contact to emit.
	 */
	void emit_singletouch(const contacts::Contact<f64> &contact) const
	{
		Vector2<f64> mean = contact.mean;

		if (m_config.invert_x)
			mean.x() = 1.0 - mean.x();

		if (m_config.invert_y)
			mean.y() = 1.0 - mean.y();

		const i32 x = gsl::narrow<i32>(std::round(mean.x() * IPTS_MAX_X));
		const i32 y = gsl::narrow<i32>(std::round(mean.y() * IPTS_MAX_Y));

		m_uinput->emit(EV_KEY, BTN_TOUCH, 1);
		m_uinput->emit(EV_ABS, ABS_X, x);
		m_uinput->emit(EV_ABS, ABS_Y, y);
	}

	/*!
	 * Lifts all currently active inputs.
	 */
	void lift_all() const
	{
		for (const usize &index : m_current) {
			m_uinput->emit(EV_ABS, ABS_MT_SLOT, gsl::narrow<i32>(index));
			this->lift_multitouch(index);
		}

		for (const usize &index : m_last) {
			m_uinput->emit(EV_ABS, ABS_MT_SLOT, gsl::narrow<i32>(index));
			this->lift_multitouch(index);
		}

		this->lift_singletouch();
	}

	/*!
	 * Commits the emitted events to the linux kernel.
	 */
	void sync() const
	{
		m_uinput->emit(EV_SYN, SYN_REPORT, 0);
	}
};

} // namespace iptsd::apps::daemon

#endif // IPTSD_APPS_DAEMON_TOUCH_HPP
