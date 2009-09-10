#include <common/buffer.h>

#include <event/action.h>
#include <event/callback.h>
#include <event/event_system.h>

#include <io/pipe.h>

#include <xcodec/xcodec.h>
#include <xcodec/xcodec_decoder.h>
#include <xcodec/xcodec_decoder_pipe.h>

XCodecDecoderPipe::XCodecDecoderPipe(XCodec *codec)
: decoder_(codec),
  input_buffer_(),
  input_eos_(false),
  output_action_(NULL),
  output_callback_(NULL)
{
}

XCodecDecoderPipe::~XCodecDecoderPipe()
{
	ASSERT(output_action_ == NULL);
	ASSERT(output_callback_ == NULL);
}

Action *
XCodecDecoderPipe::input(Buffer *buf, EventCallback *cb)
{
	if (output_callback_ != NULL) {
		ASSERT(input_buffer_.empty());
		ASSERT(output_action_ == NULL);

		if (!buf->empty()) {
			Buffer tmp;
			tmp.append(buf);
			buf->clear();

			output_callback_->event(Event(Event::Done, 0, tmp));
		} else {
			input_eos_ = true;
			output_callback_->event(Event(Event::EOS, 0));
		}
		output_action_ = EventSystem::instance()->schedule(output_callback_);
		output_callback_ = NULL;
	} else {
		if (!buf->empty()) {
			input_buffer_.append(buf);
			buf->clear();
		} else {
			input_eos_ = true;
		}
	}

	cb->event(Event(Event::Done, 0));
	return (EventSystem::instance()->schedule(cb));
}

Action *
XCodecDecoderPipe::output(EventCallback *cb)
{
	ASSERT(output_action_ == NULL);
	ASSERT(output_callback_ == NULL);

	if (!input_buffer_.empty() || input_eos_) {
		if (input_eos_) {
			cb->event(Event(Event::EOS, 0, input_buffer_));
			if (!input_buffer_.empty())
				input_buffer_.clear();
		} else {
			cb->event(Event(Event::Done, 0, input_buffer_));
			input_buffer_.clear();
		}

		return (EventSystem::instance()->schedule(cb));
	}

	output_callback_ = cb;

	return (cancellation(this, &XCodecDecoderPipe::output_cancel));
}

void
XCodecDecoderPipe::output_cancel(void)
{
	if (output_action_ != NULL) {
		ASSERT(output_callback_ == NULL);

		output_action_->cancel();
		output_action_ = NULL;
	}

	if (output_callback_ != NULL) {
		delete output_callback_;
		output_callback_ = NULL;
	}
}