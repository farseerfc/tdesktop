/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QPainter;
class QPaintEvent;

namespace Ui {
namespace Platform {

inline void StartTranslucentPaint(QPainter &p, QPaintEvent *e) {
}

inline void InitOnTopPanel(not_null<QWidget*> panel) {
}

inline void DeInitOnTopPanel(not_null<QWidget*> panel) {
}

inline void ReInitOnTopPanel(not_null<QWidget*> panel) {
}

inline void UpdateOverlayed(not_null<QWidget*> widget) {
}

inline void ShowOverAll(not_null<QWidget*> widget, bool canFocus) {
}

inline void BringToBack(not_null<QWidget*> widget) {
}

inline constexpr bool UseMainQueueGeneric() {
	return true;
}

} // namespace Platform
} // namespace Ui
