#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>
#include <atomic>

// Persistent worker threads that run the iterations of one loop together with the calling thread.
// Run(count, fn) calls fn(i) for every i in [0, count) and returns once all of them finished, so the caller sees every
// write the iterations made, as after a serial loop. Iterations are handed out one at a time from an atomic counter:
// when a worker is slow to wake up or gets preempted, the others (the caller included) take its share, so a loop never
// waits for a worker that has not started. One loop at a time: Run calls from several threads are serialized
class ParallelForPool
{
public:
	// threadInit(index) runs first on each worker thread (thread name, CPU affinity)
	explicit ParallelForPool(int workerCount, std::function<void(int)> threadInit = {})
	{
		for (int i = 0; i < workerCount; i++)
			m_threads.emplace_back(&ParallelForPool::WorkerLoop, this, i, threadInit);
	}

	~ParallelForPool()
	{
		{
			std::scoped_lock lock(m_mutex);
			m_stop = true;
		}
		m_wakeCv.notify_all();
		for (auto& t : m_threads)
			t.join();
	}

	ParallelForPool(const ParallelForPool&) = delete;
	ParallelForPool& operator=(const ParallelForPool&) = delete;

	int GetWorkerCount() const { return (int)m_threads.size(); }

	template<typename TFunc>
	void Run(int count, TFunc&& fn)
	{
		using F = std::remove_reference_t<TFunc>;
		RunImpl(count, [](void* ctx, int i) { (*(F*)ctx)(i); }, (void*)&fn);
	}

private:
	using CallFn = void (*)(void* ctx, int index);

	void RunImpl(int count, CallFn call, void* ctx)
	{
		if (count <= 0)
			return;
		if (m_threads.empty() || count == 1)
		{
			for (int i = 0; i < count; i++)
				call(ctx, i);
			return;
		}
		std::scoped_lock runLock(m_runMutex);
		{
			std::scoped_lock lock(m_mutex);
			m_call = call;
			m_ctx = ctx;
			m_count = count;
			m_next.store(0, std::memory_order_relaxed);
			m_generation++;
		}
		m_wakeCv.notify_all();
		Iterate(call, ctx, count);
		// every index is taken; wait for the workers still running one. A worker that wakes up after this sees the loop
		// closed (m_count 0) and leaves m_next alone, so the next Run can reset it
		std::unique_lock lock(m_mutex);
		m_doneCv.wait(lock, [this] { return m_active == 0; });
		m_count = 0;
	}

	void Iterate(CallFn call, void* ctx, int count)
	{
		for (int i; (i = m_next.fetch_add(1, std::memory_order_relaxed)) < count;)
			call(ctx, i);
	}

	void WorkerLoop(int index, std::function<void(int)> threadInit)
	{
		if (threadInit)
			threadInit(index);
		uint64_t seen = 0;
		std::unique_lock lock(m_mutex);
		while (true)
		{
			m_wakeCv.wait(lock, [&] { return m_stop || m_generation != seen; });
			if (m_stop)
				return;
			seen = m_generation;
			if (m_count == 0)
				continue; // woke up after the loop was done
			const CallFn call = m_call;
			void* const ctx = m_ctx;
			const int count = m_count;
			m_active++;
			lock.unlock();
			Iterate(call, ctx, count);
			lock.lock();
			if (--m_active == 0)
				m_doneCv.notify_one();
		}
	}

	std::vector<std::thread> m_threads;
	std::mutex m_runMutex; // one loop at a time
	std::mutex m_mutex; // guards everything below except m_next
	std::condition_variable m_wakeCv;
	std::condition_variable m_doneCv;
	bool m_stop = false;
	uint64_t m_generation = 0;
	CallFn m_call = nullptr;
	void* m_ctx = nullptr;
	int m_count = 0; // 0 while no loop runs
	int m_active = 0; // workers inside the current loop
	std::atomic<int> m_next{0}; // next iteration to hand out
};
