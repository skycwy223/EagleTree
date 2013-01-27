/*
 * ssd_synchronous_writer_thread.cpp
 *
 *  Created on: Jul 20, 2012
 *      Author: niv
 */

#include "../ssd.h"
#include "../MTRand/mtrand.h"

using namespace ssd;

// =================  Thread =============================

Thread::~Thread() {
	for (uint i = 0; i < threads_to_start_when_this_thread_finishes.size(); i++) {
		Thread* t = threads_to_start_when_this_thread_finishes[i];
		if (t != NULL) {
			delete t;
		}
	}
}

deque<Event*> Thread::run() {
	deque<Event*> empty;
	swap(empty, submitted_events);
	if (finished) return empty;
	//Event* event = issue_next_io();
	Event* event = issue_next_io();
	if (event != NULL) {
		submitted_events.push_back(event);
	}
	for (uint i = 0; i < submitted_events.size() && is_experiment_thread(); i++) {
		Event* e = submitted_events[i];
		if (e != NULL) e->set_experiment_io(true);
	}
	return submitted_events;
}

deque<Event*> Thread::register_event_completion(Event* event) {
	deque<Event*> empty;
	time = event->get_current_time();
	swap(empty, submitted_events);
	statistics_gatherer->register_completed_event(*event);
	handle_event_completion(event);
	if (!event->get_noop() && event->get_event_type() != TRIM) {
		num_ios_finished++;
	}
	return submitted_events;
}

void Thread::print_thread_stats() {
	printf("IOs finished by thread:  %d\n", num_ios_finished);
}

void Thread::set_os(OperatingSystem*  op_sys) {
	os = op_sys;
	op_sys->get_experiment_runtime();
}

void Thread::submit(Event* event) {
	event->set_start_time(event->get_current_time());
	submitted_events.push_front(event);
}

// =================  Synchronous_Sequential_Thread  =============================

Synchronous_Sequential_Thread::Synchronous_Sequential_Thread(long min_LBA, long max_LBA, int repetitions_num, event_type type, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  counter(0),
	  number_of_times_to_repeat(repetitions_num),
	  type(type)
{}

Event* Synchronous_Sequential_Thread::issue_next_io() {
	if (number_of_times_to_repeat > 0) {
		Event* e = new Event(type, min_LBA + counter++, 1, time);
		submit(e);
	}
	return NULL;
}

void Synchronous_Sequential_Thread::handle_event_completion(Event* event) {
	time = event->get_current_time();
	if (min_LBA + counter > max_LBA) {
		counter = 0;
		if (--number_of_times_to_repeat == 0) {
			finished = true;
		}
	}
}

// =================  Flexible_Reader_Thread  =============================

Flexible_Reader_Thread::Flexible_Reader_Thread(long min_LBA, long max_LBA, int repetitions_num, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  ready_to_issue_next_write(true),
	  number_of_times_to_repeat(repetitions_num),
	  flex_reader(NULL)
{}

Event* Flexible_Reader_Thread::issue_next_io() {
	if (flex_reader == NULL) {
		vector<Address_Range> ranges;
		ranges.push_back(Address_Range(min_LBA, max_LBA));
		assert(os != NULL);
		os->get_experiment_runtime();
		flex_reader = os->create_flexible_reader(ranges);
	}
	if (ready_to_issue_next_write && number_of_times_to_repeat > 0) {
		ready_to_issue_next_write = false;
		return flex_reader->read_next(time);
	} else {
		return NULL;
	}
}

void Flexible_Reader_Thread::handle_event_completion(Event* event) {
	assert(!ready_to_issue_next_write);
	ready_to_issue_next_write = true;
	time = event->get_current_time();
	if (flex_reader->is_finished()) {
		delete flex_reader;
		flex_reader = NULL;
		if (--number_of_times_to_repeat == 0) {
			finished = true;
			//StateVisualiser::print_page_status();
		}
	}
}

// =================  Asynchronous_Sequential_Writer  =============================

Asynchronous_Sequential_Thread::Asynchronous_Sequential_Thread(long min_LBA, long max_LBA, int repetitions_num, event_type type, double time_breaks, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  offset(0),
	  number_of_times_to_repeat(repetitions_num),
	  finished_round(false),
	  type(type),
	  number_finished(0),
	  time_breaks(time_breaks)
{}

Event* Asynchronous_Sequential_Thread::issue_next_io() {
	if (number_of_times_to_repeat == 0 || finished_round) {
		return NULL;
	}
	//time += 1;
	Event* e = new Event(type, min_LBA + offset, 1, time);
	time += 1;
	if (min_LBA + offset++ == max_LBA) {
		finished_round = true;
	}
	return e;
}

void Asynchronous_Sequential_Thread::handle_event_completion(Event* event) {
	if (number_finished++ == max_LBA - min_LBA) {
		finished_round = false;
		offset = 0;
		number_of_times_to_repeat--;
		time = event->get_current_time();
		number_finished = 0;
		//StateTracer::print();
		//StatisticsGatherer::get_instance()->print();
	}
	if (number_of_times_to_repeat == 0) {
		finished = true;
	}
}

// =================  Random_Thread  =============================

Random_Thread::Random_Thread(long min_LBA, long max_LAB, ulong randseed, int MAX_IOS, event_type type, int number_of_times_to_repeat = std::numeric_limits<int>::max())
	: Thread(0),
	  number_of_times_to_repeat(number_of_times_to_repeat),
	  type(type),
	  num_ongoing_IOs(0),
	  MAX_IOS(MAX_IOS),
	  io_gen(min_LBA, max_LAB, randseed)
{
	assert(MAX_IOS > 0);
}

Event* Random_Thread::issue_next_io() {
	bool issue = num_ongoing_IOs < MAX_IOS && number_of_times_to_repeat > 0;
	if (issue) {
		num_ongoing_IOs++;
		number_of_times_to_repeat--;
		return new Event(type, io_gen.next(), 1, time);
	}
	else return NULL;
}

void Random_Thread::handle_event_completion(Event* event) {
	time = max(time, event->get_current_time()) + 1;
	num_ongoing_IOs--;
	finished = number_of_times_to_repeat == 0 && num_ongoing_IOs == 0;
}

// =================  Asynchronous_Random_Thread_Reader_Writer  =============================

Asynchronous_Random_Thread_Reader_Writer::Asynchronous_Random_Thread_Reader_Writer(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  number_of_times_to_repeat(num_ios_to_issue),
	  random_number_generator(randseed)
{}

Event* Asynchronous_Random_Thread_Reader_Writer::issue_next_io() {
	Event* event;
	if (0 < number_of_times_to_repeat) {
		event_type type = random_number_generator() % 2 == 0 ? WRITE : READ;
		long lba = min_LBA + random_number_generator() % (max_LBA - min_LBA + 1);
		event =  new Event(type, lba, 1, time);
		//printf("Creating:  %d,  %s   ", numeric_limits<int>::max() - number_of_times_to_repeat, event->get_event_type() == WRITE ? "W" : "R"); event->print();
		time += 1;
	} else {
		event = NULL;
	}
	number_of_times_to_repeat--;
	if (number_of_times_to_repeat == 0) {
		finished = true;
	}
	return event;
}

void Asynchronous_Random_Thread_Reader_Writer::handle_event_completion(Event* event) {}

// =================  Collision_Free_Asynchronous_Random_Writer  =============================

Collision_Free_Asynchronous_Random_Thread::Collision_Free_Asynchronous_Random_Thread(long min_LBA, long max_LBA, int num_ios_to_issue, ulong randseed, event_type type, double time_breaks, double start_time)
	: Thread(start_time),
	  min_LBA(min_LBA),
	  max_LBA(max_LBA),
	  number_of_times_to_repeat(num_ios_to_issue),
	  type(type),
	  time_breaks(time_breaks),
	  random_number_generator(randseed)
{}

Event* Collision_Free_Asynchronous_Random_Thread::issue_next_io() {
	Event* event;
	if (0 < number_of_times_to_repeat) {
		long address;
		do {
			address = min_LBA + random_number_generator() % (max_LBA - min_LBA + 1);
		} while (logical_addresses_submitted.count(address) == 1);
		printf("num events submitted:  %d\n", logical_addresses_submitted.size());
		logical_addresses_submitted.insert(address);
		event =  new Event(type, address, 1, time);
		time += time_breaks;
	} else {
		event = NULL;
	}
	number_of_times_to_repeat--;
	if (number_of_times_to_repeat == 0) {
		finished = true;
	}
	return event;
}

void Collision_Free_Asynchronous_Random_Thread::handle_event_completion(Event* event) {
	logical_addresses_submitted.erase(event->get_logical_address());
}
