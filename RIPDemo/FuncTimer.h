#pragma once
#include <chrono>
#include <string>
#include <iostream>
#include "CGlobalUnits.h"

class FuncTimer
{
public:
	FuncTimer(const char* name)
		: m_name(name),
		m_start(std::chrono::high_resolution_clock::now())
	{}

	~FuncTimer()
	{
		auto end = std::chrono::high_resolution_clock::now();
		auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start).count();

		char buf[128];
		sprintf(buf, "[TIME] %s cost: %lld ms\n", m_name, cost / 1000); // 使用 %lld 打印整数
		OutputDebugStringA(buf); 
	}

private:
	const char* m_name;
	std::chrono::high_resolution_clock::time_point m_start;
};

class PerfScope
{
public:
	PerfScope(const char* name, bool bMS/* = true*/)
		: m_name(name),
		m_bMs(bMS),
		m_start(std::chrono::high_resolution_clock::now())
	{}

	~PerfScope()
	{
		auto end = std::chrono::high_resolution_clock::now();
		auto cost = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start).count();

		char buf[128];
		if (m_bMs)
			sprintf(buf, "[TIME] %s cost: %lld ms\n", m_name, cost / 1000); // 使用 %lld 打印整数
		else
			sprintf(buf, "[TIME] %s cost: %lld us\n", m_name, cost); // 使用 %lld 打印整数
		OutputDebugStringA(buf);
	}

private:
	const char* m_name;
	bool m_bMs = true;
	std::chrono::high_resolution_clock::time_point m_start;
};
