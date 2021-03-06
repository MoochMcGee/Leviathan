#include "WindowsSupport/WindowsSupport.pkg"
#include "Debug/Debug.pkg"

//----------------------------------------------------------------------------------------
//Structures
//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> struct RandomTimeAccessBuffer<DataType, TimesliceType>::TimesliceEntry
{
	TimesliceType timesliceLength;
	bool committed;
};

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> struct RandomTimeAccessBuffer<DataType, TimesliceType>::WriteEntry
{
	WriteEntry()
	{}
	WriteEntry(const DataType& defaultValue)
	:newValue(defaultValue)
	{}
	WriteEntry(unsigned int awriteAddress, TimesliceType awriteTime, const DataType& anewValue, const Timeslice& acurrentTimeslice)
	:writeAddress(awriteAddress), writeTime(awriteTime), newValue(anewValue), currentTimeslice(acurrentTimeslice)
	{}

	unsigned int writeAddress;
	TimesliceType writeTime;
	DataType newValue;
	Timeslice currentTimeslice;
};

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> struct RandomTimeAccessBuffer<DataType, TimesliceType>::TimesliceSaveEntry
{
	TimesliceSaveEntry(typename const std::list<TimesliceEntry>::const_iterator& atimeslice, unsigned int aid)
	:timeslice(atimeslice), id(aid)
	{}
	TimesliceSaveEntry(unsigned int aid, TimesliceType atimesliceLength)
	:id(aid), timesliceLength(atimesliceLength)
	{}
	typename std::list<TimesliceEntry>::const_iterator timeslice;
	Timeslice timesliceLoad;
	unsigned int id;
	TimesliceType timesliceLength;
};

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> struct RandomTimeAccessBuffer<DataType, TimesliceType>::WriteSaveEntry
{
	WriteSaveEntry(unsigned int awriteAddress, TimesliceType awriteTime, const DataType& aoldValue, unsigned int acurrentTimeslice)
	:writeAddress(awriteAddress), writeTime(awriteTime), oldValue(aoldValue), currentTimeslice(acurrentTimeslice)
	{}
	unsigned int writeAddress;
	TimesliceType writeTime;
	DataType oldValue;
	unsigned int currentTimeslice;
};

//----------------------------------------------------------------------------------------
//Constructors
//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> RandomTimeAccessBuffer<DataType, TimesliceType>::RandomTimeAccessBuffer()
:latestMemoryBufferExists(false)
{}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> RandomTimeAccessBuffer<DataType, TimesliceType>::RandomTimeAccessBuffer(const DataType& adefaultValue)
:defaultValue(adefaultValue)
{}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> RandomTimeAccessBuffer<DataType, TimesliceType>::RandomTimeAccessBuffer(unsigned int size, bool akeepLatestCopy)
:latestMemoryBufferExists(akeepLatestCopy)
{
	memory.resize(size);
	if(latestMemoryBufferExists)
	{
		latestMemory.resize(size);
	}
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> RandomTimeAccessBuffer<DataType, TimesliceType>::RandomTimeAccessBuffer(unsigned int size, bool akeepLatestCopy, const DataType& adefaultValue)
:defaultValue(adefaultValue), latestMemoryBufferExists(akeepLatestCopy)
{
	memory.resize(size, defaultValue);
	if(latestMemoryBufferExists)
	{
		latestMemory.resize(size, defaultValue);
	}
}

//----------------------------------------------------------------------------------------
//Size functions
//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> unsigned int RandomTimeAccessBuffer<DataType, TimesliceType>::Size() const
{
	return (unsigned int)memory.size();
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::Resize(unsigned int size, bool akeepLatestCopy)
{
	std::unique_lock<std::mutex> lock(accessLock);
	latestMemoryBufferExists = akeepLatestCopy;
	memory.resize(size, defaultValue);
	if(latestMemoryBufferExists)
	{
		latestMemory.resize(size);
	}
	else
	{
		latestMemory.clear();
	}
}

//----------------------------------------------------------------------------------------
//Access functions
//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> DataType RandomTimeAccessBuffer<DataType, TimesliceType>::Read(unsigned int address, const AccessTarget& accessTarget) const
{
	switch(accessTarget.target)
	{
	case accessTarget.TARGET_COMMITTED:
		return ReadCommitted(address);
	case accessTarget.TARGET_COMMITTED_TIME:
		return ReadCommitted(address, accessTarget.time);
	case accessTarget.TARGET_LATEST:
		return ReadLatest(address);
	case accessTarget.TARGET_TIME:
		return Read(address, accessTarget.time);
	}
	DebugAssert(false);
	return DataType(defaultValue);
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::Write(unsigned int address, const DataType& data, const AccessTarget& accessTarget)
{
	switch(accessTarget.target)
	{
	case accessTarget.TARGET_COMMITTED:
		WriteCommitted(address, data);
		return;
	case accessTarget.TARGET_LATEST:
		WriteLatest(address, data);
		return;
	case accessTarget.TARGET_TIME:
		Write(address, accessTarget.time, data);
		return;
	}
	DebugAssert(false);
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> DataType RandomTimeAccessBuffer<DataType, TimesliceType>::Read(unsigned int address, const TimedBufferAccessTarget<DataType, TimesliceType>* accessTarget) const
{
	switch(accessTarget.target)
	{
	case accessTarget.TARGET_COMMITTED:
		return ReadCommitted(address);
	case accessTarget.TARGET_COMMITTED_TIME:
		return ReadCommitted(address, accessTarget.time);
	case accessTarget.TARGET_LATEST:
		return ReadLatest(address);
	case accessTarget.TARGET_TIME:
		return Read(address, accessTarget.time);
	}
	DebugAssert(false);
	return DataType(defaultValue);
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::Write(unsigned int address, const DataType& data, const TimedBufferAccessTarget<DataType, TimesliceType>* accessTarget)
{
	switch(accessTarget.target)
	{
	case accessTarget.TARGET_COMMITTED:
		WriteCommitted(address, data);
		return;
	case accessTarget.TARGET_LATEST:
		WriteLatest(address, data);
		return;
	case accessTarget.TARGET_TIME:
		Write(address, accessTarget.time, data);
		return;
	}
	DebugAssert(false);
}

//----------------------------------------------------------------------------------------
//This function starts at the end of the buffered writes, and works its way back to the
//beginning, stopping when it finds a write to the target address. We do this to optimize
//ad-hoc reads of a buffer when there are a large number of reads and writes occurring to
//a small number of addresses.
//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> DataType RandomTimeAccessBuffer<DataType, TimesliceType>::Read(unsigned int address, TimesliceType readTime) const
{
	std::unique_lock<std::mutex> lock(accessLock);

	//Search for written values in the current timeslice
	std::list<WriteEntry>::const_reverse_iterator i = writeList.rbegin();
	while((i != writeList.rend()) && (i->currentTimeslice == latestTimeslice))
	{
		if((i->writeAddress == address) && (i->writeTime <= readTime))
		{
			return i->newValue;
		}
		++i;
	}
	//Search for written values in previous timeslices
	while(i != writeList.rend())
	{
		if(i->writeAddress == address)
		{
			return i->newValue;
		}
		++i;
	}

	//Default to the committed value
	return memory[address];
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::Write(unsigned int address, TimesliceType writeTime, const DataType& data)
{
	std::unique_lock<std::mutex> lock(accessLock);

	WriteEntry entry(address, writeTime, data, latestTimeslice);

	//Find the correct location in the list to insert the new write entry. The writeList
	//must be sorted from earliest to latest write by time.
	bool updateLatestBufferContents = true;
	std::list<WriteEntry>::reverse_iterator i = writeList.rbegin();
	while((i != writeList.rend()) && (i->currentTimeslice == latestTimeslice) && (i->writeTime > writeTime))
	{
		if(latestMemoryBufferExists && (i->writeAddress == address))
		{
			updateLatestBufferContents = false;
		}
		++i;
	}
	writeList.insert(i.base(), entry);

	//If we're holding a cached copy of the latest memory state, update it.
	if(latestMemoryBufferExists && updateLatestBufferContents)
	{
		latestMemory[address] = data;
	}
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> DataType& RandomTimeAccessBuffer<DataType, TimesliceType>::ReferenceCommitted(unsigned int address)
{
	return memory[address];
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> DataType RandomTimeAccessBuffer<DataType, TimesliceType>::ReadCommitted(unsigned int address) const
{
	return memory[address];
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> DataType RandomTimeAccessBuffer<DataType, TimesliceType>::ReadCommitted(unsigned int address, TimesliceType readTime) const
{
	std::unique_lock<std::mutex> lock(accessLock);
	TimesliceType currentTimeBase = 0;

	//Default to the committed value
	DataType foundValue = memory[address];

	//Search for any buffered writes before the read time
	std::list<TimesliceEntry>::const_iterator currentTimeslice = timesliceList.begin();
	std::list<WriteEntry>::const_iterator i = writeList.begin();
	bool done = false;
	while((i != writeList.end()) && !done)
	{
		//Advance through the timeslice list until we find the timeslice matching the
		//next buffered write, or we pass the end of this time step.
		while((currentTimeslice != i->currentTimeslice)
			&& (((currentTimeBase + currentTimeslice->timesliceLength) - currentTimeOffset) <= readTime))
		{
			currentTimeBase += currentTimeslice->timesliceLength;
			++currentTimeslice;
		}
		//If the next buffered write is outside the time step, terminate processing of
		//buffered writes.
		if((currentTimeslice != i->currentTimeslice) || (((currentTimeBase + i->writeTime) - currentTimeOffset) > readTime))
		{
			done = true;
			continue;
		}
		//If the write occurred to the same address as our target, set it as the new
		//value for our target address at the time of the read.
		if(i->writeAddress == address)
		{
			foundValue = i->newValue;
		}
		++i;
	}

	return foundValue;
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::WriteCommitted(unsigned int address, const DataType& data)
{
	//##NOTE## We don't update the latest memory buffer state here, since it would be very
	//costly in performance to do so, and the premise of this function is kind of flawed
	//to begin with. I strongly recommend removing this function entirely. We shouldn't
	//need it anywhere, and the only place it is actually called right now is the old VDP
	//core, and incorrectly too from what I can tell. Calling WriteLatest is the correct
	//thing to do in this kind of case, not WriteCommitted.
	memory[address] = data;
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> DataType RandomTimeAccessBuffer<DataType, TimesliceType>::ReadLatest(unsigned int address) const
{
	//If we don't have a cached copy of the latest memory state saved, determine the
	//latest value for the target memory address by iterating through the uncommitted
	//write list.
	if(!latestMemoryBufferExists)
	{
		std::unique_lock<std::mutex> lock(accessLock);

		//Search for written values in any timeslice
		std::list<WriteEntry>::const_reverse_iterator i = writeList.rbegin();
		while(i != writeList.rend())
		{
			if(i->writeAddress == address)
			{
				return i->newValue;
			}
			++i;
		}

		//Default to the committed value
		return memory[address];
	}

	//Return the cached latest value for this memory address
	return latestMemory[address];
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::WriteLatest(unsigned int address, const DataType& data)
{
	std::unique_lock<std::mutex> lock(accessLock);

	//Erase any write entries to this address in any timeslice. We do this to prevent
	//uncommitted writes from overwriting this change. This write function should make
	//the new value visible from all access functions.
	std::list<WriteEntry>::iterator i = writeList.begin();
	while(i != writeList.end())
	{
		if(i->writeAddress == address)
		{
			writeList.erase(i++);
		}
		else
		{
			++i;
		}
	}

	//Write the new value directly to the committed state
	memory[address] = data;

	//If we're holding a cached copy of the latest memory state, update it.
	if(latestMemoryBufferExists)
	{
		latestMemory[address] = data;
	}
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::GetLatestBufferCopy(std::vector<DataType>& buffer) const
{
	if(!latestMemoryBufferExists)
	{
		std::unique_lock<std::mutex> lock(accessLock);

		//Resize the target buffer to match the size of the source buffer, and populate
		//with the committed memory state.
		buffer.assign(memory.begin(), memory.end());

		//Commit each buffered write entry to the target buffer
		for(std::list<WriteEntry>::const_iterator i = writeList.begin(); i != writeList.end(); ++i)
		{
			buffer[i->writeAddress] = i->newValue;
		}
	}
	else
	{
		//Resize the target buffer to match the size of the source buffer, and populate
		//with the latest memory state.
		buffer.assign(latestMemory.begin(), latestMemory.end());
	}
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::GetLatestBufferCopy(DataType* buffer, unsigned int bufferSize) const
{
	//Determine the number of elements to copy
	size_t copySize = (size_t)bufferSize;
	if(copySize > memory.size())
	{
		copySize = memory.size();
	}

	if(!latestMemoryBufferExists)
	{
		std::unique_lock<std::mutex> lock(accessLock);

		//Populate the target buffer with the committed memory state
		memcpy((void*)buffer, (const void*)&memory[0], (size_t)copySize * sizeof(DataType));

		//Commit each buffered write entry to the target buffer
		for(std::list<WriteEntry>::const_iterator i = writeList.begin(); i != writeList.end(); ++i)
		{
			buffer[i->writeAddress] = i->newValue;
		}
	}
	else
	{
		//Populate the target buffer with the latest memory state
		memcpy((void*)buffer, (const void*)&latestMemory[0], (size_t)copySize * sizeof(DataType));
	}
}

//----------------------------------------------------------------------------------------
//Time management functions
//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::Initialize()
{
	std::unique_lock<std::mutex> lock(accessLock);

	//Initialize buffers
	for(unsigned int i = 0; i < memory.size(); ++i)
	{
		memory[i] = defaultValue;
	}
	if(latestMemoryBufferExists)
	{
		for(unsigned int i = 0; i < latestMemory.size(); ++i)
		{
			latestMemory[i] = defaultValue;
		}
	}
	writeList.clear();
	timesliceList.clear();
	currentTimeOffset = 0;
	latestTimeslice = timesliceList.end();
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> bool RandomTimeAccessBuffer<DataType, TimesliceType>::DoesLatestTimesliceExist() const
{
	std::unique_lock<std::mutex> lock(accessLock);
	return !timesliceList.empty();
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> typename RandomTimeAccessBuffer<DataType, TimesliceType>::Timeslice RandomTimeAccessBuffer<DataType, TimesliceType>::GetLatestTimeslice()
{
	std::unique_lock<std::mutex> lock(accessLock);

	if(timesliceList.empty())
	{
		return timesliceList.end();
	}
	else
	{
		return (++timesliceList.rbegin()).base();
	}
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::AdvancePastTimeslice(const Timeslice& targetTimeslice)
{
	std::unique_lock<std::mutex> lock(accessLock);

	//Commit buffered writes which we have passed in this step
	std::list<TimesliceEntry>::iterator currentTimeslice = timesliceList.begin();
	std::list<WriteEntry>::iterator i = writeList.begin();
	bool done = false;
	while((i != writeList.end()) && !done)
	{
		//Advance through the timeslice list until we find the timeslice matching the
		//next buffered write, or we pass the end of this time step.
		while((currentTimeslice != i->currentTimeslice) && (currentTimeslice != targetTimeslice))
		{
			++currentTimeslice;
		}
		//If the next buffered write is outside the time step, terminate processing of
		//buffered writes.
		if(currentTimeslice != i->currentTimeslice)
		{
			done = true;
			continue;
		}
		memory[i->writeAddress] = i->newValue;
		++i;
	}
	//Check for any remaining timeslices which have expired
	while(currentTimeslice != targetTimeslice)
	{
		++currentTimeslice;
	}
	//Set our current time offset to the end of the target timeslice
	currentTimeOffset = targetTimeslice->timesliceLength;

	//Erase buffered writes which have been committed, and timeslices which have expired.
	writeList.erase(writeList.begin(), i);
	timesliceList.erase(timesliceList.begin(), targetTimeslice);
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::AdvanceToTimeslice(const Timeslice& targetTimeslice)
{
	std::unique_lock<std::mutex> lock(accessLock);

	//Commit buffered writes which we have passed in this step
	std::list<TimesliceEntry>::iterator currentTimeslice = timesliceList.begin();
	std::list<WriteEntry>::iterator i = writeList.begin();
	bool done = false;
	while((i != writeList.end()) && !done)
	{
		//Advance through the timeslice list until we find the timeslice matching the
		//next buffered write, or we pass the end of this time step.
		while((currentTimeslice != i->currentTimeslice) && (currentTimeslice != targetTimeslice))
		{
			++currentTimeslice;
		}
		//If the next buffered write is outside the time step, terminate processing of
		//buffered writes.
		if((currentTimeslice != i->currentTimeslice) || (currentTimeslice == targetTimeslice))
		{
			done = true;
			continue;
		}
		memory[i->writeAddress] = i->newValue;
		++i;
	}
	//Check for any remaining timeslices which have expired
	while(currentTimeslice != targetTimeslice)
	{
		++currentTimeslice;
	}
	//Set our current time offset to the start of the target timeslice
	currentTimeOffset = 0;

	//Erase buffered writes which have been committed, and timeslices which have expired.
	writeList.erase(writeList.begin(), i);
	timesliceList.erase(timesliceList.begin(), targetTimeslice);
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::AdvanceByTime(TimesliceType step, const Timeslice& targetTimeslice)
{
	std::unique_lock<std::mutex> lock(accessLock);

	TimesliceType currentTimeBase = 0;

	//Commit buffered writes which we have passed in this step
	std::list<TimesliceEntry>::iterator currentTimeslice = timesliceList.begin();
	std::list<WriteEntry>::iterator i = writeList.begin();
	bool done = false;
	while((i != writeList.end()) && !done)
	{
		//Advance through the timeslice list until we find the timeslice matching the
		//next buffered write, or we pass the end of this time step.
		while((currentTimeslice != i->currentTimeslice)
			&& (currentTimeslice != targetTimeslice)
			&& (((currentTimeBase + currentTimeslice->timesliceLength) - currentTimeOffset) <= step))
		{
			currentTimeBase += currentTimeslice->timesliceLength;
			++currentTimeslice;
		}
		//If the next buffered write is outside the time step, terminate processing of
		//buffered writes.
		if((currentTimeslice != i->currentTimeslice) || (((currentTimeBase + i->writeTime) - currentTimeOffset) > step))
		{
			done = true;
			continue;
		}
		memory[i->writeAddress] = i->newValue;
		++i;
	}
	//Check for any remaining timeslices which have expired
	while((currentTimeslice != targetTimeslice)
		&& (((currentTimeBase + currentTimeslice->timesliceLength) - currentTimeOffset) <= step))
	{
		currentTimeBase += currentTimeslice->timesliceLength;
		++currentTimeslice;
	}
	//Set the amount of the current timeslice which has been stepped through as the time
	//offset for the next step operation.
	currentTimeOffset = (currentTimeOffset + step) - currentTimeBase;

	//Erase buffered writes which have been committed, and timeslices which have expired.
	writeList.erase(writeList.begin(), i);
	timesliceList.erase(timesliceList.begin(), currentTimeslice);
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> bool RandomTimeAccessBuffer<DataType, TimesliceType>::AdvanceByStep(const Timeslice& targetTimeslice)
{
	std::unique_lock<std::mutex> lock(accessLock);

	TimesliceType currentTimeBase = 0;
	TimesliceType writeTime = targetTimeslice->timesliceLength;
	bool foundWrite = false;

	//Commit buffered writes which we have passed in this step
	std::list<TimesliceEntry>::iterator currentTimeslice = timesliceList.begin();
	std::list<WriteEntry>::iterator i = writeList.begin();
	if(!writeList.empty())
	{
		//Advance through the timeslice list until we find the timeslice matching the
		//next buffered write, or we pass the end of this time step.
		while((currentTimeslice != i->currentTimeslice)
			&& (currentTimeslice != targetTimeslice))
		{
			currentTimeBase += currentTimeslice->timesliceLength;
			++currentTimeslice;
		}
		//If the next buffered write is within the time step, save the write time and
		//commit the data.
		if(currentTimeslice == i->currentTimeslice)
		{
			foundWrite = true;
			writeTime = i->writeTime;
			memory[i->writeAddress] = i->newValue;
			++i;
		}
	}
	//Check for any remaining timeslices which have expired.
	if(!foundWrite)
	{
		while(currentTimeslice != targetTimeslice)
		{
			currentTimeBase += currentTimeslice->timesliceLength;
			++currentTimeslice;
		}
	}

	//Set the amount of the current timeslice which has been stepped through as the time
	//offset for the next step operation.
	currentTimeOffset = writeTime;

	//Erase buffered writes which have been committed, and timeslices which have expired.
	writeList.erase(writeList.begin(), i);
	timesliceList.erase(timesliceList.begin(), currentTimeslice);

	return foundWrite;
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::AdvanceBySession(TimesliceType currentProgress, AdvanceSession& advanceSession, const Timeslice& targetTimeslice)
{
	//Note that we split the internals of this method outside this inline wrapper function
	//for performance. If we fold all the logic into one method, we can't effectively
	//inline it, and we get a big performance penalty in the case that this test fails,
	//which we expect it will almost all the time, due to a lack of inlining and needing
	//to prepare the stack and registers for inner variables that never get used. This has
	//been verified through profiling as a performance bottleneck.
	if(currentProgress >= advanceSession.nextWriteTime)
	{
		AdvanceBySessionInternal(currentProgress, advanceSession, targetTimeslice);
	}
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::AdvanceBySessionInternal(TimesliceType currentProgress, AdvanceSession& advanceSession, const Timeslice& targetTimeslice)
{
	//Since a write needs to be processed, obtain a lock, and loop around until there
	//are no writes left within the update step.
	std::unique_lock<std::mutex> lock(accessLock);
	advanceSession.writeInfo.exists = false;
	bool done = false;
	while(!done && (currentProgress >= advanceSession.nextWriteTime))
	{
		TimesliceType step = (((currentProgress + advanceSession.initialTimeOffset) - advanceSession.timeRemovedDuringSession) - currentTimeOffset);
		TimesliceType currentTimeBase = 0;

		//Commit buffered writes which we have passed in this step
		std::list<TimesliceEntry>::iterator currentTimeslice = timesliceList.begin();
		std::list<WriteEntry>::iterator i = writeList.begin();
		bool foundNextWrite = false;
		bool reachedEndOfTargetTimeslice = false;
		while((i != writeList.end()) && !foundNextWrite && !reachedEndOfTargetTimeslice)
		{
			//Advance through the timeslice list until we find the timeslice matching
			//the next buffered write, or we pass the end of this time step.
			while((currentTimeslice != i->currentTimeslice)
				&& (currentTimeslice != targetTimeslice)
				&& (((currentTimeBase + currentTimeslice->timesliceLength) - currentTimeOffset) <= step))
			{
				currentTimeBase += currentTimeslice->timesliceLength;
				++currentTimeslice;
			}
			//If the next buffered write is outside the time step, terminate
			//processing of buffered writes.
			if((currentTimeslice != i->currentTimeslice))
			{
				reachedEndOfTargetTimeslice = true;
				continue;
			}
			if(((currentTimeBase + i->writeTime) - currentTimeOffset) > step)
			{
				//We capture the next write time here, so we have it to perform the
				//next step in this session.
				advanceSession.nextWriteTime = (advanceSession.timeRemovedDuringSession + currentTimeBase + i->writeTime) - advanceSession.initialTimeOffset;
				foundNextWrite = true;

				//If the caller has requested full write info to be retrieved for the
				//next write, populate the writeInfo structure.
				if(advanceSession.retrieveWriteInfo)
				{
					//Note that we construct a new WriteInfo structure to overwrite
					//the old one to ensure that the newValue member is correctly
					//constructed. We can't guarantee the caller correctly constructed
					//this member using the same default value as was passed to this
					//container. We construct it again here to take the burden off the
					//caller.
					advanceSession.writeInfo = WriteInfo(true, i->writeAddress, advanceSession.nextWriteTime, i->newValue);
				}
				continue;
			}
			//If the next buffered write has been passed during this update, commit
			//it, and advance to the next write.
			memory[i->writeAddress] = i->newValue;
			++i;
		}
		//Check for any remaining timeslices which have expired
		while((currentTimeslice != targetTimeslice)
			&& (((currentTimeBase + currentTimeslice->timesliceLength) - currentTimeOffset) <= step))
		{
			currentTimeBase += currentTimeslice->timesliceLength;
			++currentTimeslice;
		}
		//Set the amount of the current timeslice which has been stepped through as
		//the time offset for the next step operation.
		currentTimeOffset = (currentTimeOffset + step) - currentTimeBase;

		//If we didn't find the next write after the current step, set the next write
		//time to be the end of the target timeslice, and explicitly break out of the
		//advance loop.
		if(!foundNextWrite)
		{
			TimesliceType timeTillEndOfTargetTimeslice = 0;
			std::list<TimesliceEntry>::iterator tempTimeslice = currentTimeslice;
			while(tempTimeslice != targetTimeslice)
			{
				timeTillEndOfTargetTimeslice += tempTimeslice->timesliceLength;
				++tempTimeslice;
			}
			timeTillEndOfTargetTimeslice += tempTimeslice->timesliceLength;
			advanceSession.nextWriteTime = (advanceSession.timeRemovedDuringSession + currentTimeBase + timeTillEndOfTargetTimeslice) - advanceSession.initialTimeOffset;
			done = true;
		}

		//Erase buffered writes which have been committed, and timeslices which have
		//expired.
		writeList.erase(writeList.begin(), i);
		timesliceList.erase(timesliceList.begin(), currentTimeslice);

		//If we've just removed some timeslices as a result of this step, advance the
		//base address of the session.
		advanceSession.timeRemovedDuringSession += (currentTimeBase - advanceSession.initialTimeOffset);
		advanceSession.initialTimeOffset = 0;
	}
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> TimesliceType RandomTimeAccessBuffer<DataType, TimesliceType>::GetNextWriteTimeNoLock(const Timeslice& targetTimeslice) const
{
	bool foundWrite = false;
	TimesliceType nextWriteTime = 0;
	TimesliceType currentTimeBase = 0;

	//Search the write list for the next buffered write inside this time step
	std::list<TimesliceEntry>::const_iterator currentTimeslice = timesliceList.begin();
	std::list<WriteEntry>::const_iterator i = writeList.begin();
	if(!writeList.empty())
	{
		//Advance through the timeslice list until we find the timeslice matching the
		//next buffered write, or we pass the end of this time step.
		while((currentTimeslice != i->currentTimeslice)
			&& (currentTimeslice != targetTimeslice))
		{
			currentTimeBase += currentTimeslice->timesliceLength;
			++currentTimeslice;
		}
		//If the next buffered write is within the time step, save the write time.
		if(currentTimeslice == i->currentTimeslice)
		{
			foundWrite = true;
			nextWriteTime = ((currentTimeBase + i->writeTime) - currentTimeOffset);
		}
	}
	//If no write is pending within the time step, output the end of the target timeslice
	//as the next write time.
	if(!foundWrite)
	{
		while(currentTimeslice != targetTimeslice)
		{
			currentTimeBase += currentTimeslice->timesliceLength;
			++currentTimeslice;
		}
		nextWriteTime = ((currentTimeBase + currentTimeslice->timesliceLength) - currentTimeOffset);
	}

	return nextWriteTime;
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> TimesliceType RandomTimeAccessBuffer<DataType, TimesliceType>::GetNextWriteTime(const Timeslice& targetTimeslice) const
{
	std::unique_lock<std::mutex> lock(accessLock);
	return GetNextWriteTimeNoLock(targetTimeslice);
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> typename RandomTimeAccessBuffer<DataType, TimesliceType>::WriteInfo RandomTimeAccessBuffer<DataType, TimesliceType>::GetWriteInfo(unsigned int index, const Timeslice& targetTimeslice)
{
	std::unique_lock<std::mutex> lock(accessLock);

	TimesliceType currentTimeBase = 0;
	unsigned int currentIndex = 0;
	WriteInfo writeInfo(defaultValue);
	writeInfo.exists = false;

	//Search the write list for the next buffered write inside this time step
	std::list<TimesliceEntry>::iterator currentTimeslice = timesliceList.begin();
	std::list<WriteEntry>::iterator i = writeList.begin();
	bool done = false;
	while((i != writeList.end()) && !done)
	{
		//Advance through the timeslice list until we find the timeslice matching the
		//next buffered write, or we pass the end of this time step.
		while((currentTimeslice != i->currentTimeslice)
			&& (currentTimeslice != targetTimeslice))
		{
			currentTimeBase += currentTimeslice->timesliceLength;
			++currentTimeslice;
		}
		//If the next buffered write is within the time step, output its data
		if(currentTimeslice == i->currentTimeslice)
		{
			if(currentIndex == index)
			{
				writeInfo.exists = true;
				writeInfo.writeAddress = i->writeAddress;
				writeInfo.newValue = i->newValue;
				writeInfo.writeTime = ((currentTimeBase + i->writeTime) - currentTimeOffset);
			}
			else
			{
				++currentIndex;
				continue;
			}
		}
		done = true;
	}

	return writeInfo;
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::Commit()
{
	std::unique_lock<std::mutex> lock(accessLock);

	//Flag all timeslices as committed
	std::list<TimesliceEntry>::reverse_iterator i = timesliceList.rbegin();
	while((i != timesliceList.rend()) && (!i->committed))
	{
		i->committed = true;
		++i;
	}
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::Rollback()
{
	std::unique_lock<std::mutex> lock(accessLock);

	//Erase non-committed memory writes
	std::list<WriteEntry>::reverse_iterator i = writeList.rbegin();
	while((i != writeList.rend()) && (!i->currentTimeslice->committed))
	{
		++i;
	}
	writeList.erase(i.base(), writeList.end());

	//Erase non-committed timeslice entries
	std::list<TimesliceEntry>::reverse_iterator j = timesliceList.rbegin();
	while((j != timesliceList.rend()) && (!j->committed))
	{
		++j;
	}
	timesliceList.erase(j.base(), timesliceList.end());

	//Recalculate the latest timeslice
	if(timesliceList.empty())
	{
		latestTimeslice = timesliceList.end();
	}
	else
	{
		latestTimeslice = (++timesliceList.rbegin()).base();
	}

	//If we're caching the latest memory state, rebuild the buffer contents.
	if(latestMemoryBufferExists)
	{
		//Populate the latest memory buffer with the committed memory state
		latestMemory.assign(memory.begin(), memory.end());

		//Commit each buffered write entry to the latest memory buffer
		for(std::list<WriteEntry>::const_iterator i = writeList.begin(); i != writeList.end(); ++i)
		{
			latestMemory[i->writeAddress] = i->newValue;
		}
	}
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::AddTimeslice(TimesliceType timeslice)
{
	std::unique_lock<std::mutex> lock(accessLock);

	//Add the new timeslice entry to the list
	TimesliceEntry entry;
	entry.timesliceLength = timeslice;
	entry.committed = false;
	timesliceList.push_back(entry);

	//Select the new timeslice entry as the latest timeslice
	latestTimeslice = (++timesliceList.rbegin()).base();
}

//----------------------------------------------------------------------------------------
//Session management functions
//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> void RandomTimeAccessBuffer<DataType, TimesliceType>::BeginAdvanceSession(AdvanceSession& advanceSession, const Timeslice& targetTimeslice, bool retrieveWriteInfo) const
{
	std::unique_lock<std::mutex> lock(accessLock);

	//Record whether we want to retrieve the full write info for steps in this session
	advanceSession.retrieveWriteInfo = retrieveWriteInfo;
	advanceSession.writeInfo.exists = false;

	//Initialize the base time settings for this session
	advanceSession.timeRemovedDuringSession = 0;
	advanceSession.initialTimeOffset = currentTimeOffset;

	//Get the next write time, relative to the start of this session.
	advanceSession.nextWriteTime = GetNextWriteTimeNoLock(targetTimeslice);
}

//----------------------------------------------------------------------------------------
//Savestate functions
//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> bool RandomTimeAccessBuffer<DataType, TimesliceType>::LoadState(IHierarchicalStorageNode& node)
{
	std::list<TimesliceSaveEntry> timesliceSaveList;
	std::list<WriteSaveEntry> writeSaveList;

	//Load the current time offset
	node.ExtractAttribute(L"CurrentTimeOffset", currentTimeOffset);

	//Read saved data from the XML tree
	std::list<IHierarchicalStorageNode*> childList = node.GetChildList();
	for(std::list<IHierarchicalStorageNode*>::iterator i = childList.begin(); i != childList.end(); ++i)
	{
		if((*i)->GetName() == L"TimesliceList")
		{
			if(!LoadTimesliceEntries(*(*i), timesliceSaveList))
			{
				return false;
			}
		}
		else if((*i)->GetName() == L"WriteList")
		{
			if(!LoadWriteEntries(*(*i), writeSaveList))
			{
				return false;
			}
		}
	}

	//Load timeslice list
	timesliceList.clear();
	for(std::list<TimesliceSaveEntry>::iterator i = timesliceSaveList.begin(); i != timesliceSaveList.end(); ++i)
	{
		TimesliceEntry timesliceEntry;
		timesliceEntry.timesliceLength = i->timesliceLength;
		timesliceList.push_back(timesliceEntry);
		i->timesliceLoad = (++timesliceList.rbegin()).base();
	}

	//Recalculate the latest timeslice
	if(timesliceList.empty())
	{
		latestTimeslice = timesliceList.end();
	}
	else
	{
		latestTimeslice = (++timesliceList.rbegin()).base();
	}

	//Load memory buffer
	node.ExtractBinaryData(memory);

	//Load write list, and rebuild memory buffer
	writeList.clear();
	for(std::list<WriteSaveEntry>::reverse_iterator i = writeSaveList.rbegin(); i != writeSaveList.rend(); ++i)
	{
		WriteEntry writeEntry(defaultValue);
		writeEntry.writeAddress = i->writeAddress;
		writeEntry.writeTime = i->writeTime;
		std::list<TimesliceSaveEntry>::iterator currentTimeslice = timesliceSaveList.begin();
		while((currentTimeslice != timesliceSaveList.end()) && (currentTimeslice->id != i->currentTimeslice))
		{
			++currentTimeslice;
		}
		if(currentTimeslice != timesliceSaveList.end())
		{
			writeEntry.currentTimeslice = currentTimeslice->timesliceLoad;
			writeEntry.newValue = memory[writeEntry.writeAddress];
			memory[writeEntry.writeAddress] = i->oldValue;
			writeList.push_front(writeEntry);
		}
	}

	//If we're caching the latest memory state, rebuild the buffer contents.
	if(latestMemoryBufferExists)
	{
		//Populate the latest memory buffer with the committed memory state
		latestMemory.assign(memory.begin(), memory.end());

		//Commit each buffered write entry to the latest memory buffer
		for(std::list<WriteEntry>::const_iterator i = writeList.begin(); i != writeList.end(); ++i)
		{
			latestMemory[i->writeAddress] = i->newValue;
		}
	}

	return true;
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> bool RandomTimeAccessBuffer<DataType, TimesliceType>::LoadTimesliceEntries(IHierarchicalStorageNode& node, std::list<TimesliceSaveEntry>& timesliceSaveList)
{
	std::list<IHierarchicalStorageNode*> childList = node.GetChildList();
	for(std::list<IHierarchicalStorageNode*>::iterator i = childList.begin(); i != childList.end(); ++i)
	{
		if((*i)->GetName() == L"Timeslice")
		{
			IHierarchicalStorageAttribute* timesliceID = (*i)->GetAttribute(L"TimesliceID");
			IHierarchicalStorageAttribute* timesliceLength = (*i)->GetAttribute(L"TimesliceLength");
			if((timesliceID != 0) && (timesliceLength != 0))
			{
				TimesliceSaveEntry entry(timesliceID->ExtractValue<unsigned int>(), timesliceLength->ExtractValue<TimesliceType>());

				//Find the correct location in the list to insert the timeslice entry. The
				//timeslice list must be sorted from earliest to latest by id.
				std::list<TimesliceSaveEntry>::reverse_iterator j = timesliceSaveList.rbegin();
				while((j != timesliceSaveList.rend()) && (j->id > entry.id))
				{
					++j;
				}
				timesliceSaveList.insert(j.base(), entry);
			}
		}
	}

	return true;
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> bool RandomTimeAccessBuffer<DataType, TimesliceType>::LoadWriteEntries(IHierarchicalStorageNode& node, std::list<WriteSaveEntry>& writeSaveList)
{
	std::list<IHierarchicalStorageNode*> childList = node.GetChildList();
	for(std::list<IHierarchicalStorageNode*>::iterator i = childList.begin(); i != childList.end(); ++i)
	{
		if((*i)->GetName() == L"Write")
		{
			IHierarchicalStorageAttribute* writeAddress = (*i)->GetAttribute(L"WriteAddress");
			IHierarchicalStorageAttribute* writeTime = (*i)->GetAttribute(L"WriteTime");
			IHierarchicalStorageAttribute* oldValue = (*i)->GetAttribute(L"OldValue");
			IHierarchicalStorageAttribute* timesliceID = (*i)->GetAttribute(L"TimesliceID");
			if((writeAddress != 0) && (writeTime != 0) && (oldValue != 0) && (timesliceID != 0))
			{
				//Extract the write entry from the XML stream
				DataType oldValueData(defaultValue);
				oldValue->ExtractValue(oldValueData);
				WriteSaveEntry entry(writeAddress->ExtractValue<unsigned int>(), writeTime->ExtractValue<TimesliceType>(), oldValueData, timesliceID->ExtractValue<unsigned int>());

				//Find the correct location in the list to insert the write entry. The
				//write list must be sorted from earliest to latest.
				std::list<WriteSaveEntry>::reverse_iterator j = writeSaveList.rbegin();
				while((j != writeSaveList.rend()) && ((j->currentTimeslice > entry.currentTimeslice) || ((j->currentTimeslice == entry.currentTimeslice) && (j->writeTime > entry.writeTime))))
				{
					++j;
				}
				writeSaveList.insert(j.base(), entry);
			}
		}
	}

	return true;
}

//----------------------------------------------------------------------------------------
template<class DataType, class TimesliceType> bool RandomTimeAccessBuffer<DataType, TimesliceType>::SaveState(IHierarchicalStorageNode& node, const std::wstring& bufferName, bool inlineData) const
{
	std::list<TimesliceSaveEntry> timesliceSaveList;
	std::vector<DataType> saveMemory;

	//Save the current time offset
	node.CreateAttribute(L"CurrentTimeOffset", currentTimeOffset);

	//Take a copy of the current memory buffer. We need to copy the contents rather than
	//directly dumping the current memory buffer, because we change the direction of the
	//write history when we save it. Rather than saving the committed state of the
	//memory, we save the latest volatile state of the memory, and each writeEntry
	//contains the previous data that was in the memory before that write occurred,
	//rather than the new value which is being written. We do this to allow easy
	//modification of the saved data, without having to worry about uncommitted writes
	//overwriting changes.
	saveMemory.insert(saveMemory.begin(), memory.begin(), memory.end());

	//Build numeric ID's to identify each timeslice, and save the timeslice list state
	IHierarchicalStorageNode& timesliceListState = node.CreateChild(L"TimesliceList");
	unsigned int id = 0;
	for(std::list<TimesliceEntry>::const_iterator i = timesliceList.begin(); i != timesliceList.end(); ++i)
	{
		timesliceSaveList.push_back(TimesliceSaveEntry(i, id));
		IHierarchicalStorageNode& timesliceEntry = timesliceListState.CreateChild(L"Timeslice");
		timesliceEntry.CreateAttribute(L"TimesliceID", id);
		timesliceEntry.CreateAttribute(L"TimesliceLength", i->timesliceLength);
		++id;
	}

	//Save the writeList state
	IHierarchicalStorageNode& writeListState = node.CreateChild(L"WriteList");
	std::list<TimesliceSaveEntry>::iterator currentTimeslice = timesliceSaveList.begin();
	for(std::list<WriteEntry>::const_iterator i = writeList.begin(); i != writeList.end(); ++i)
	{
		IHierarchicalStorageNode& writeEntry = writeListState.CreateChild(L"Write");
		while(currentTimeslice->timeslice != i->currentTimeslice)
		{
			++currentTimeslice;
		}
		writeEntry.CreateAttribute(L"TimesliceID", currentTimeslice->id);
		writeEntry.CreateAttribute(L"WriteAddress", i->writeAddress);
		writeEntry.CreateAttribute(L"WriteTime", i->writeTime);
		writeEntry.CreateAttribute(L"OldValue", saveMemory[i->writeAddress]);
		saveMemory[i->writeAddress] = i->newValue;
	}

	//Add the memory buffer to the XML tree
	node.InsertBinaryData(saveMemory, bufferName, inlineData);

	return true;
}
