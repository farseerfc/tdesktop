/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_media_preview.h"

#include "data/data_photo.h"
#include "data/data_document.h"
#include "ui/image/image.h"
#include "ui/emoji_config.h"
#include "lottie/lottie_single_player.h"
#include "main/main_session.h"
#include "chat_helpers/stickers.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_history.h"

namespace Window {
namespace {

constexpr int kStickerPreviewEmojiLimit = 10;

} // namespace

MediaPreviewWidget::MediaPreviewWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _emojiSize(Ui::Emoji::GetSizeLarge() / cIntRetinaFactor()) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	subscribe(_controller->session().downloaderTaskFinished(), [=] {
		update();
	});
}

QRect MediaPreviewWidget::updateArea() const {
	const auto size = currentDimensions();
	return QRect(
		QPoint((width() - size.width()) / 2, (height() - size.height()) / 2),
		size);
}

void MediaPreviewWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r(e->rect());

	const auto image = [&] {
		if (!_lottie || !_lottie->ready()) {
			return QImage();
		}
		_lottie->markFrameShown();
		return _lottie->frame();
	}();
	const auto pixmap = image.isNull() ? currentImage() : QPixmap();
	const auto size = image.isNull() ? pixmap.size() : image.size();
	int w = size.width() / cIntRetinaFactor(), h = size.height() / cIntRetinaFactor();
	auto shown = _a_shown.value(_hiding ? 0. : 1.);
	if (!_a_shown.animating()) {
		if (_hiding) {
			hide();
			_controller->disableGifPauseReason(Window::GifPauseReason::MediaPreview);
			return;
		}
	} else {
		p.setOpacity(shown);
//		w = qMax(qRound(w * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(w % 2), 1);
//		h = qMax(qRound(h * (st::stickerPreviewMin + ((1. - st::stickerPreviewMin) * shown)) / 2.) * 2 + int(h % 2), 1);
	}
	p.fillRect(r, st::stickerPreviewBg);
	if (image.isNull()) {
		p.drawPixmap((width() - w) / 2, (height() - h) / 2, pixmap);
	} else {
		p.drawImage(
			QRect((width() - w) / 2, (height() - h) / 2, w, h),
			image);
	}
	if (!_emojiList.empty()) {
		const auto emojiCount = _emojiList.size();
		const auto emojiWidth = (emojiCount * _emojiSize) + (emojiCount - 1) * st::stickerEmojiSkip;
		auto emojiLeft = (width() - emojiWidth) / 2;
		const auto esize = Ui::Emoji::GetSizeLarge();
		for (const auto emoji : _emojiList) {
			Ui::Emoji::Draw(
				p,
				emoji,
				esize,
				emojiLeft,
				(height() - h) / 2 - (_emojiSize * 2));
			emojiLeft += _emojiSize + st::stickerEmojiSkip;
		}
	}
}

void MediaPreviewWidget::resizeEvent(QResizeEvent *e) {
	update();
}

void MediaPreviewWidget::showPreview(
		Data::FileOrigin origin,
		not_null<DocumentData*> document) {
	if (!document
		|| (!document->isAnimation() && !document->sticker())
		|| document->isVideoMessage()) {
		hidePreview();
		return;
	}

	startShow();
	_origin = origin;
	_photo = nullptr;
	_document = document;
	fillEmojiString();
	resetGifAndCache();
}

void MediaPreviewWidget::showPreview(
		Data::FileOrigin origin,
		not_null<PhotoData*> photo) {
	startShow();
	_origin = origin;
	_photo = photo;
	_document = nullptr;
	fillEmojiString();
	resetGifAndCache();
}

void MediaPreviewWidget::startShow() {
	_cache = QPixmap();
	if (isHidden() || _a_shown.animating()) {
		if (isHidden()) {
			show();
			_controller->enableGifPauseReason(Window::GifPauseReason::MediaPreview);
		}
		_hiding = false;
		_a_shown.start([this] { update(); }, 0., 1., st::stickerPreviewDuration);
	} else {
		update();
	}
}

void MediaPreviewWidget::hidePreview() {
	if (isHidden()) {
		return;
	}
	if (_gif) _cache = currentImage();
	_hiding = true;
	_a_shown.start([this] { update(); }, 1., 0., st::stickerPreviewDuration);
	_photo = nullptr;
	_document = nullptr;
	resetGifAndCache();
}

void MediaPreviewWidget::fillEmojiString() {
	_emojiList.clear();
	if (_photo) {
		return;
	}
	if (auto sticker = _document->sticker()) {
		if (auto list = Stickers::GetEmojiListFromSet(_document)) {
			_emojiList = std::move(*list);
			while (_emojiList.size() > kStickerPreviewEmojiLimit) {
				_emojiList.pop_back();
			}
		} else if (const auto emoji = Ui::Emoji::Find(sticker->alt)) {
			_emojiList.emplace_back(emoji);
		}
	}
}

void MediaPreviewWidget::resetGifAndCache() {
	_lottie = nullptr;
	_gif.reset();
	_cacheStatus = CacheNotLoaded;
	_cachedSize = QSize();
}

QSize MediaPreviewWidget::currentDimensions() const {
	if (!_cachedSize.isEmpty()) {
		return _cachedSize;
	}
	if (!_document && !_photo) {
		_cachedSize = QSize(_cache.width() / cIntRetinaFactor(), _cache.height() / cIntRetinaFactor());
		return _cachedSize;
	}

	QSize result, box;
	if (_photo) {
		result = QSize(_photo->width(), _photo->height());
		box = QSize(width() - 2 * st::boxVerticalMargin, height() - 2 * st::boxVerticalMargin);
	} else {
		result = _document->dimensions;
		if (_gif && _gif->ready()) {
			result = QSize(_gif->width(), _gif->height());
		}
		if (_document->sticker()) {
			box = QSize(st::maxStickerSize, st::maxStickerSize);
		} else {
			box = QSize(2 * st::maxStickerSize, 2 * st::maxStickerSize);
		}
	}
	result = QSize(qMax(style::ConvertScale(result.width()), 1), qMax(style::ConvertScale(result.height()), 1));
	if (result.width() > box.width()) {
		result.setHeight(qMax((box.width() * result.height()) / result.width(), 1));
		result.setWidth(box.width());
	}
	if (result.height() > box.height()) {
		result.setWidth(qMax((box.height() * result.width()) / result.height(), 1));
		result.setHeight(box.height());
	}
	if (_photo) {
		_cachedSize = result;
	}
	return result;
}

void MediaPreviewWidget::setupLottie() {
	Expects(_document != nullptr);

	_lottie = std::make_unique<Lottie::SinglePlayer>(
		Lottie::ReadContent(_document->data(), _document->filepath()),
		Lottie::FrameRequest{ currentDimensions() * cIntRetinaFactor() },
		Lottie::Quality::High);

	_lottie->updates(
	) | rpl::start_with_next([=](Lottie::Update update) {
		update.data.match([&](const Lottie::Information &) {
			this->update();
		}, [&](const Lottie::DisplayFrameRequest &) {
			this->update(updateArea());
		});
	}, lifetime());
}

QPixmap MediaPreviewWidget::currentImage() const {
	if (_document) {
		if (const auto sticker = _document->sticker()) {
			if (_cacheStatus != CacheLoaded) {
				if (sticker->animated && !_lottie && _document->loaded()) {
					const_cast<MediaPreviewWidget*>(this)->setupLottie();
				}
				if (_lottie && _lottie->ready()) {
					return QPixmap();
				} else if (const auto image = _document->getStickerLarge()) {
					QSize s = currentDimensions();
					_cache = image->pix(_origin, s.width(), s.height());
					_cacheStatus = CacheLoaded;
				} else if (_cacheStatus != CacheThumbLoaded
					&& _document->hasThumbnail()
					&& _document->thumbnail()->loaded()) {
					QSize s = currentDimensions();
					_cache = _document->thumbnail()->pixBlurred(_origin, s.width(), s.height());
					_cacheStatus = CacheThumbLoaded;
				}
			}
		} else {
			_document->automaticLoad(_origin, nullptr);
			if (_document->loaded()) {
				if (!_gif && !_gif.isBad()) {
					auto that = const_cast<MediaPreviewWidget*>(this);
					that->_gif = Media::Clip::MakeReader(_document, FullMsgId(), [=](Media::Clip::Notification notification) {
						that->clipCallback(notification);
					});
					if (_gif) _gif->setAutoplay();
				}
			}
			if (_gif && _gif->started()) {
				auto s = currentDimensions();
				auto paused = _controller->isGifPausedAtLeastFor(Window::GifPauseReason::MediaPreview);
				return _gif->current(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None, paused ? 0 : crl::now());
			}
			if (_cacheStatus != CacheThumbLoaded
				&& _document->hasThumbnail()) {
				QSize s = currentDimensions();
				if (_document->thumbnail()->loaded()) {
					_cache = _document->thumbnail()->pixBlurred(_origin, s.width(), s.height());
					_cacheStatus = CacheThumbLoaded;
				} else if (const auto blurred = _document->thumbnailInline()) {
					_cache = _document->thumbnail()->pixBlurred(_origin, s.width(), s.height());
					_cacheStatus = CacheThumbLoaded;
				} else {
					_document->thumbnail()->load(_origin);
				}
			}
		}
	} else if (_photo) {
		if (_cacheStatus != CacheLoaded) {
			if (_photo->loaded()) {
				QSize s = currentDimensions();
				_cache = _photo->large()->pix(_origin, s.width(), s.height());
				_cacheStatus = CacheLoaded;
			} else {
				_photo->load(_origin);
				if (_cacheStatus != CacheThumbLoaded) {
					QSize s = currentDimensions();
					if (_photo->thumbnail()->loaded()) {
						_cache = _photo->thumbnail()->pixBlurred(_origin, s.width(), s.height());
						_cacheStatus = CacheThumbLoaded;
					} else if (_photo->thumbnailSmall()->loaded()) {
						_cache = _photo->thumbnailSmall()->pixBlurred(_origin, s.width(), s.height());
						_cacheStatus = CacheThumbLoaded;
					} else if (const auto blurred = _photo->thumbnailInline()) {
						_cache = blurred->pixBlurred(_origin, s.width(), s.height());
						_cacheStatus = CacheThumbLoaded;
					} else {
						_photo->thumbnailSmall()->load(_origin);
					}
				}
			}
		}

	}
	return _cache;
}

void MediaPreviewWidget::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case NotificationReinit: {
		if (_gif && _gif->state() == State::Error) {
			_gif.setBad();
		}

		if (_gif && _gif->ready() && !_gif->started()) {
			QSize s = currentDimensions();
			_gif->start(s.width(), s.height(), s.width(), s.height(), ImageRoundRadius::None, RectPart::None);
		}

		update();
	} break;

	case NotificationRepaint: {
		if (_gif && !_gif->currentDisplayed()) {
			update(updateArea());
		}
	} break;
	}
}

MediaPreviewWidget::~MediaPreviewWidget() {
}

} // namespace Window
