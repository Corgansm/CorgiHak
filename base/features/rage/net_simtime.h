#pragma once
// Every simulation time the server sent per player, in arrival order, captured in the m_flSimulationTime recv proxy.
// The entity's own value can't be used for defensive detection: the animation system writes the previous simulation
// time back on updates without new animation, and several updates can arrive between two of our checks.
#include <algorithm>
#include <array>

namespace NetSim {
	constexpr int Capacity{ 16 };

	struct Queue_t {
		std::array< float, Capacity > m_arrValues{ };
		int m_iCount{ };
	};

	inline std::array< Queue_t, 65 > Queues{ };

	inline void Note( int index, float simTime ) {
		if ( index < 1 || index > 64 )
			return;

		auto& queue{ Queues[ index ] };
		if ( queue.m_iCount == Capacity ) {
			// keep the newest: drop the oldest value
			std::rotate( queue.m_arrValues.begin( ), queue.m_arrValues.begin( ) + 1, queue.m_arrValues.end( ) );
			queue.m_iCount = Capacity - 1;
		}
		queue.m_arrValues[ queue.m_iCount++ ] = simTime;
	}

	// copies the queued values out ( oldest first ) and empties the queue
	inline int Drain( int index, std::array< float, Capacity >& out ) {
		if ( index < 1 || index > 64 )
			return 0;

		auto& queue{ Queues[ index ] };
		const int count{ queue.m_iCount };
		std::copy_n( queue.m_arrValues.begin( ), count, out.begin( ) );
		queue.m_iCount = 0;
		return count;
	}
}
