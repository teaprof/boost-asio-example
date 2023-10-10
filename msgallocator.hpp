#ifndef MSGALLOCATOR_HPP
#define MSGALLOCATOR_HPP

//This class contains a list of Containers like std::vector and finds a
//container with most appropriate capacity on demand. In this way, using of this
//class can reduce the number of system calls such as malloc and free (new and delete).
//The internal logic is very simple, it works good when maxTotalCapacity is
//greater than average container capacity, and capacity is approximately
//the same for all containers.
template<class Container=Buffer>
class PreallocatedBuffers
{
public:
    PreallocatedBuffers(size_t _maxTotalCapacity = 1<<20) :
        maxTotalCapacity(_maxTotalCapacity), curTotalCapacity(0)
    {

    }

    void push(Container&& buf)
    {
        if(curTotalCapacity + buf.capacity() < maxTotalCapacity)
        {
            mBuffers.emplace_back(std::move(buf));
            curTotalCapacity += buf.capacity();
        }
    }
    void find(size_t minimalSize, Container& buf)
    {
        size_t bestIdx = 0, bestSize = 0;
        bool found = false;
        //find container with minimal size but not less than minimalSize
        for(size_t curIdx = 0; curIdx < mBuffers.size(); curIdx++)
        {
            size_t curSize = mBuffers[curIdx].size();
            if(curSize >= minimalSize)
            {
                if(found)
                {
                    if(curSize < bestSize)
                    {
                        bestIdx = curIdx;
                        bestSize = curSize;
                        if(bestSize == minimalSize)
                            break;
                    }
                } else {
                    found = true;
                    bestIdx = curIdx;
                    bestSize = curSize;
                    if(bestSize == minimalSize)
                        break;
                }
            }
        }
        if(found)
        {
            auto& mybuf = mBuffers[bestIdx];
            curTotalCapacity -= mybuf.capacity();
            if(curTotalCapacity + buf.capacity() <= maxTotalCapacity)
            {
                curTotalCapacity += buf.capacity();
                buf.swap(mybuf);
            } else {
                buf.swap(mybuf);
                mBuffers.erase(mBuffers.begin() + bestIdx);
            }
        } else {
            buf.reserve(minimalSize);
        }
    }
private:
    size_t maxTotalCapacity, curTotalCapacity;
    std::vector<Container> mBuffers;
};

#endif // MSGALLOCATOR_HPP
