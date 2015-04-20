#pragma once

#include "tpltrick.h"
#include "const.h"
#include <vector128.h>

//
// Light-weight input/output queue
//
template<class T, size_t N, size_t M, size_t SIZE = lcm<N,M>::value, size_t NSTREAM = 1> // input burst N, output burst M, buffer size SIZE, streams number NSTREAM
class TPinQueue {
public:
    typedef T DataType;
    static const size_t rsize = M;
    static const size_t wsize = N;
    static const size_t qsize = SIZE;
    static const size_t nstream = NSTREAM;

private:
    A16 T buf_ [NSTREAM][SIZE];
    unsigned int w_cnt;
    unsigned int r_cnt;

public:
    TPinQueue () {
        // compilation-time check: the buffer size is at least LCM of M and N
        CCASSERT ((SIZE >= lcm<M,N>::value));
        CCASSERT (NSTREAM > 0);
        w_cnt = r_cnt = 0;
    };
    
    FINL bool check_read () const {
        return (w_cnt - r_cnt >= M);
    }

    FINL void pop () {
        r_cnt += M;
        assert(r_cnt <= w_cnt);

        if (r_cnt == w_cnt) {
            r_cnt = w_cnt = 0;
        }
    }

    FINL const T *peek (size_t iss = 0) const { return (const_cast<TPinQueue <T, N, M, SIZE, NSTREAM> *>(this))->peek(iss); }

    FINL T *peek (size_t iss = 0)
    {
        return buf_[iss] + r_cnt;
    }
    
    FINL T *write (size_t iss = 0)
    {
		assert(w_cnt < SIZE);
        return buf_[iss] + w_cnt;
    }

	FINL T *append () {
		assert(w_cnt < SIZE);
        T *pi = buf_[0] + w_cnt;
        w_cnt += N;
        assert(w_cnt <= SIZE);
        return pi;
	}

    // Pad default items, in order to statisfy input burst size of downstream brick so can continue process
    // remaining items in this PinQueue. This is useful during flushing of bricks whose output burst size larger than 1.
    // Note: there is empty definition of this function in the specialized 1:1 version
    // Returns:
    //   flase if no need to pad (determined at compilation time, so conservative at runtime), otherwise true
    FINL bool pad (const T& item = T()) {
        if (wsize % rsize == 0) return false; // optimizable, no runtime overhead
        int npad = ceil_pad(count(), rsize);

        int i; size_t iss;
        for (i = 0; i < npad; i++)
        {
            for (iss = 0; iss < NSTREAM; iss++)
                buf_[iss][w_cnt] = item;
            w_cnt++;
        }
        return true;
    }

    FINL void clear () {
        w_cnt = r_cnt = 0;
    }

    FINL void zerobuf() {
        memset(buf_, 0, sizeof(buf_));
    }

    size_t count() const {
        return w_cnt - r_cnt;
    }
};

// specialize the N:N
template<class T, size_t N, size_t NSTREAM> // input/output burst N
class TPinQueue <T, N, N, N, NSTREAM> {
public:
    typedef T DataType;
    static const size_t rsize = N;
    static const size_t wsize = N;
    static const size_t qsize = N;
    static const size_t nstream = NSTREAM;

private:
    A16 T buf_[NSTREAM][N];
    int cnt_;
    
public:
    TPinQueue () : cnt_(0) { }

    FINL bool check_read () const { return (cnt_>0); }
	
    FINL void pop () {
        cnt_ = 0;
    }

    FINL const T *peek (size_t iss = 0) const { return (const_cast<TPinQueue <T, N, N, N, NSTREAM> *>(this))->peek(iss); }

    FINL T *peek (size_t iss = 0)
    {
        return buf_[iss];
    }
    
    FINL T *write (size_t iss = 0)
    {
        assert(cnt_ == 0);
        return buf_[iss];
    }

	FINL T *append () {
		assert(cnt_ == 0);
        T *pi = buf_[0];
        cnt_ = N;
        return pi;
	}
	
    FINL bool pad (const T& item = T()) {
        return false;
    }

    FINL void clear () {
        cnt_ = 0;
    }
    
    FINL void zerobuf() {
        memset(buf_, 0, sizeof(buf_));
    }

    size_t count() const {
        return cnt_;
    }
};

template<class TYPE, size_t BURST = 1, size_t NSTREAM = 1>
struct iport_traits
{
    // Note: if type is void, it represent generic type, matching any oport_tratis' type
    typedef TYPE type;
    static const size_t burst = BURST;
    static const size_t nstream  = NSTREAM;
};

template<class TYPE, size_t BURST = 1, size_t NSTREAM = 1>
struct oport_traits
{
    typedef TYPE type;
    static const size_t burst = BURST;
    static const size_t nstream  = NSTREAM;
};

typedef TPinQueue<int, 1, 1> DummyPinQueue;
typedef iport_traits<int, 1, 1> DummyIPortTraits;
typedef oport_traits<int, 1, 1> DummyOPortTraits;

template<class OPORT_TRAITS, class IPORT_TRAITS>
class DeducedPinQueue
{
public:
    typedef typename TPinQueue<typename OPORT_TRAITS::type // use upstream port type
        , OPORT_TRAITS::burst
        , IPORT_TRAITS::burst
        , lcm<OPORT_TRAITS::burst, IPORT_TRAITS::burst>::value
        , OPORT_TRAITS::nstream // use upstream port NSTREAM
    > type;
};

// Partial specialization of the template class DeducedPinQueue, for ambigous iport
template<class OPORT_TRAITS>
struct DeducedPinQueue<OPORT_TRAITS, iport_traits<void> >
{
    typedef typename TPinQueue<typename OPORT_TRAITS::type
        , OPORT_TRAITS::burst
        , OPORT_TRAITS::burst
        , OPORT_TRAITS::burst
        , OPORT_TRAITS::nstream
    > type;
};

// Partial specialization of the template class DeducedPinQueue, for ambigous oport
template<class IPORT_TRAITS>
struct DeducedPinQueue<oport_traits<void>, IPORT_TRAITS>
{
    typedef typename TPinQueue<typename IPORT_TRAITS::type
        , IPORT_TRAITS::burst
        , IPORT_TRAITS::burst
        , IPORT_TRAITS::burst
        , IPORT_TRAITS::nstream
    > type;
};

// Partial specialization of the template class DeducedPinQueue, for ambigous iport
template<>
struct DeducedPinQueue<oport_traits<void>, iport_traits<void> >
{
    // Note: Not supporting generic programming of brick in general
    // Below is just to make compiler happy
    typedef DummyPinQueue type;
};
