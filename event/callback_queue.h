/*
 * Copyright (c) 2010-2011 Juli Mallett. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	EVENT_CALLBACK_QUEUE_H
#define	EVENT_CALLBACK_QUEUE_H

#include <deque>

#include <common/thread/mutex.h>

#include <event/callback.h>

class CallbackQueue : public CallbackScheduler {
	class CallbackAction : public Cancellable {
	public:
		CallbackQueue *const queue_;
		const uint64_t generation_;
		CallbackBase *callback_;

		CallbackAction(CallbackQueue *queue, uint64_t generation, CallbackBase *callback)
		: Cancellable(),
		  queue_(queue),
		  generation_(generation),
		  callback_(callback)
		{ }

		~CallbackAction()
		{
			delete callback_;
			callback_ = NULL;
		}

		void cancel(void)
		{
			queue_->cancel(this);
		}
	};

	friend class CallbackAction;

	Mutex mtx_;
	std::deque<CallbackAction *> queue_;
	uint64_t generation_;
public:
	CallbackQueue(void)
	: mtx_("CallbackQueue"),
	  queue_(),
	  generation_(0)
	{ }

	~CallbackQueue()
	{
		ASSERT("/callback/queue", queue_.empty());
	}

	Action *schedule(CallbackBase *cb)
	{
		CallbackAction *a = new CallbackAction(this, generation_, cb);
		mtx_.lock();
		queue_.push_back(a);
		mtx_.unlock();
		return (a);
	}

	/*
	 * Runs all callbacks that have already been queued, but none that
	 * are added by callbacks that are called as part of the drain
	 * operation.  Returns true if there are queued callbacks that were
	 * added during drain.
	 */
	bool drain(void)
	{
		mtx_.lock();
		if (queue_.empty()) {
			mtx_.unlock();
			return (false);
		}

		generation_++;
		while (!queue_.empty()) {
			CallbackAction *a = queue_.front();
			ASSERT("/callback/queue", a->queue_ == this);
			if (a->generation_ >= generation_) {
				mtx_.unlock();
				return (true);
			}
			mtx_.unlock();
			a->callback_->execute();
			mtx_.lock();
		}
		return (false);
	}

	/* XXX Is not const because of the Mutex.  */
	bool empty(void)
	{
		ScopedLock _(&mtx_);
		return (queue_.empty());
	}

	void perform(void)
	{
		mtx_.lock();
		if (queue_.empty()) {
			mtx_.unlock();
			return;
		}
		CallbackAction *a = queue_.front();
		ASSERT("/callback/queue", a->queue_ == this);
		mtx_.unlock();
		/*
		 * XXX
		 * This can race with cancel, right?
		 * We need some way to flag that a callback
		 * is in progress, and ensure we do some kind
		 * of right thing in that case.  Or maybe it's
		 * up to the application at some point if it's
		 * running with multiple threads to handle?
		 * Although that doesn't seem very convincing.
		 */
		a->callback_->execute();
	}

private:
	void cancel(CallbackAction *a)
	{
		std::deque<CallbackAction *>::iterator it;

		ScopedLock _(&mtx_);
		for (it = queue_.begin(); it != queue_.end(); ++it) {
			if (*it != a)
				continue;
			queue_.erase(it);
			return;
		}
		NOTREACHED("/callback/queue");
	}
};

#endif /* !EVENT_CALLBACK_QUEUE_H */
