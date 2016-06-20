/*
	Copyright (C) 2009-2015 DeSmuME team

	This file is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with the this software.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <3ds.h>
#include <stdio.h>

#include "../utils/task.h"

// http://stackoverflow.com/questions/150355/programmatically-find-the-number-of-cores-on-a-machine
int getOnlineCores (void)
{
	u8 isN3DS;
	APT_CheckNew3DS(&isN3DS);
	if(isN3DS)
		return 3;

	return 1;
}

class Task::Impl {
private:
	Thread _thread;
	bool _isThreadRunning;
	
public:
	Impl();
	~Impl();

	void start(bool spinlock);
	void execute(const TWork &work, void *param);
	void* finish();
	void shutdown();

	//slock_t *mutex;
	Handle condWork;
	TWork workFunc;
	void *workFuncParam;
	void *ret;
	bool exitThread;
};

static void taskProc(void *arg)
{
	Task::Impl *ctx = (Task::Impl *)arg;
	do {

		while (ctx->workFunc == NULL && !ctx->exitThread) {
			svcWaitSynchronization(ctx->condWork, U64_MAX);
		}

		if (ctx->workFunc != NULL) {
			ctx->ret = ctx->workFunc(ctx->workFuncParam);
		} else {
			ctx->ret = NULL;
		}

		ctx->workFunc = NULL;
		svcClearEvent(ctx->condWork);

	} while(!ctx->exitThread);
}

Task::Impl::Impl()
{
	_isThreadRunning = false;
	workFunc = NULL;
	workFuncParam = NULL;
	ret = NULL;
	exitThread = false;

	//mutex = slock_new();
	svcCreateEvent(&condWork,0);
}

Task::Impl::~Impl()
{
	shutdown();
	svcCloseHandle(condWork);
}

void Task::Impl::start(bool spinlock)
{
	if (this->_isThreadRunning) {
		return;
	}

	this->workFunc = NULL;
	this->workFuncParam = NULL;
	this->ret = NULL;
	this->exitThread = false;
	this->_thread = threadCreate(taskProc, this, 4 * 1024 * 1024, 0x18, 2, true);
	this->_isThreadRunning = true;

}

void Task::Impl::execute(const TWork &work, void *param)
{
	printf("execute");

	if (work == NULL || !this->_isThreadRunning) {
		return;
	}

	this->workFunc = work;
	this->workFuncParam = param;

	svcSignalEvent(this->condWork);
}

void* Task::Impl::finish()
{
	void *returnValue = NULL;

	if (!this->_isThreadRunning) {
		return returnValue;
	}

	while (this->workFunc != NULL) {
		svcSleepThread(1);
	}

	returnValue = this->ret;

	//slock_unlock(this->mutex);

	return returnValue;
}

void Task::Impl::shutdown()
{

	if (!this->_isThreadRunning) {
		return;
	}

	this->workFunc = NULL;
	this->exitThread = true;
	
	svcSignalEvent(this->condWork);
	threadJoin(_thread, U64_MAX);

	this->_isThreadRunning = false;
}

void Task::start(bool spinlock) { impl->start(spinlock); }
void Task::shutdown() { impl->shutdown(); }
Task::Task() : impl(new Task::Impl()) {}
Task::~Task() { delete impl; }
void Task::execute(const TWork &work, void* param) { impl->execute(work,param); }
void* Task::finish() { return impl->finish(); }


