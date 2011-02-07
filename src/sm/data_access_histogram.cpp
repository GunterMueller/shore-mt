/* -*- mode:C++; c-basic-offset:4 -*-
   Shore-kits -- Benchmark implementations for Shore-MT
   
   Copyright (c) 2007-2009
   Data Intensive Applications and Systems Labaratory (DIAS)
   Ecole Polytechnique Federale de Lausanne
   
   All Rights Reserved.
   
   Permission to use, copy, modify and distribute this software and
   its documentation is hereby granted, provided that both the
   copyright notice and this permission notice appear in all copies of
   the software, derivative works or modified versions, and any
   portions thereof, and that both notices appear in supporting
   documentation.
   
   This code is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. THE AUTHORS
   DISCLAIM ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
   RESULTING FROM THE USE OF THIS SOFTWARE.
*/

/** @file:   data_access_histogram.cpp
 *
 *  @brief:  Implementation of the data_access_histogram used by
 *           load balancing system.
 *
 *  @date:   February 2011
 *
 *  @author: Pinar Tozun (pinar)
 *  @author: Ippokratis Pandis (ipandis)
 */

#ifdef __GNUG__
#           pragma implementation "data_access_histogram.h"
#endif

#include "data_access_histogram.h"




/****************************************************************** 
 *
 * Construction/Destruction
 *
 * @brief: The constuctor calls the default initialization function
 * @brief: The destructor needs to free all the malloc'ed keys
 *
 ******************************************************************/

data_access_histogram::data_access_histogram()
    : _is_local(false)
{
    _foo_keys.clear();
}


data_access_histogram::data_access_histogram(key_ranges_map& krm,
					     const int common_granularity,
					     const bool is_local)
{
    initialize(krm, common_granularity, is_local);
}


void data_access_histogram::initialize(key_ranges_map& krm,
				       const int common_granularity,
				       const bool is_local)
{
    _is_local = is_local;
	
    // TODO: pin: currently i don't know how to partition a range
    //            if i try to go over the bits i'll have the same
    //            msb/lsb related problems. think of a way to solve
    //            this. otherwise, you're left with integers again
    //            in the worst case
    //            below is a not very nice temporary solution

    // TODO: pin: if no local ones are going to be used remove is_local also
    
    _foo_keys.clear();
    uint init = 0;
    vector<lpid_t> subtrees;
    w_rc_t r = krm.getAllPartitions(subtrees);
    if (r.is_error()) { W_FATAL(r.err_num()); }

    if(!is_local) {
	_histogram_lock.acquire_write();
    }

    // initialize the maps
    for(uint i=0; i < subtrees.size(); i++) {
	foo* newkv = new foo((char*) (&init), sizeof(int), true);
	_range_accesses[subtrees[i]][*newkv] = 0;
	//_range_locks[subtrees[i]][*newkv] = occ_rwlock();
	_foo_keys[subtrees[i]].push_back(newkv);
	_granularities[subtrees[i]] = common_granularity;
    }
    
    if(!is_local) {
	_histogram_lock.release_write();
    }
}


data_access_histogram::~data_access_histogram()
{
    // Delete the allocated keys in the map

    DBG(<<"Destroying the data access histogram: ");
    
    if(!_is_local) {
	_histogram_lock.acquire_write();
    } else {
	// TODO: put the values this has to the global histogram
	//       or you can do this when sdesc is gone but be
	//       careful to do it only once
    }

    // pin: temp: write the values to a file to check now
    ofstream data_accesses("data_accesses.txt", ios::app);
    for(ranges_hist_iter ranges_iter = _range_accesses.begin();
	ranges_iter != _range_accesses.end();
	ranges_iter++) {
	data_accesses << endl;
	data_accesses << ranges_iter->first << endl;
	for(sub_ranges_hist_iter sub_ranges_iter = (ranges_iter->second).begin();
	    sub_ranges_iter != (ranges_iter->second).end();
	    sub_ranges_iter++) {
	    data_accesses << "\t" << (sub_ranges_iter->first)._m << " : " << sub_ranges_iter->second << endl;
	}
    }
    
    
    for(key_values_iter keys_iter = _foo_keys.begin();
	keys_iter != _foo_keys.end();
	keys_iter++) {
	for(sub_key_values_iter sub_keys_iter = (keys_iter->second).begin();
	    sub_keys_iter != (keys_iter->second).end();
	    sub_keys_iter++) {
	    if (*sub_keys_iter) { 
		delete (*sub_keys_iter);
		*sub_keys_iter = NULL;
	    }
	}
    }

    if(!_is_local) {
	_histogram_lock.release_write();
    }
}



//// accesses map management  ////


/****************************************************************** 
 *
 * @fn:      inc_access_count()
 *
 * @brief:   increments the access count of the range that the key belongs to by 1
 *
 * @param:   lpid_t root  - root of the subtree
 * @param:   cvec_t key   - the accessed key
 *
 ******************************************************************/

w_rc_t data_access_histogram::inc_access_count(const lpid_t& root, const Key& key)
{
    w_rc_t r = RCOK;
    foo kv((char*)key._base[0].ptr,key._base[0].len,false);
    //sub_ranges_lock_iter sub_lock_iter; 

    // 1. acquire the locks if necessary
    if(!_is_local) {
	_histogram_lock.acquire_read();
	_root_locks[root].acquire_read();
	//W_DO(_acquire_lock(root, kv, sub_lock_iter, true));
    }

    // 2. update the range bucket
    ranges_hist_iter ranges_iter = _range_accesses.find(root);
    if(ranges_iter == _range_accesses.end()) {
	r = RC(hist_ROOT_DOESNT_EXIST);
    } else {
	sub_ranges_hist_iter sub_ranges_iter = (ranges_iter->second).lower_bound(kv);
	if(sub_ranges_iter == (ranges_iter->second).end()) {
	    r = RC(hist_RANGE_DOESNT_EXIST);
	} else {
	    atomic_inc(sub_ranges_iter->second);
	}
    }

    // 3. release the locks if necessary
    if(!_is_local) {
	//W_DO(_release_lock(sub_ranges_iter, true));
	_root_locks[root].release_read();
	_histogram_lock.release_read();
    }
    
    return (r);
}
	

/****************************************************************** 
 *
 * @fn:      update_access_count()
 *
 * @brief:   updates the access count of the range that the key belings to by the given amount
 *
 * @param:   lpid_t root  - root of the subtree
 * @param:   cvec_t key   - the accessed key
 * @param:   int amount   - the amount to increment the access count of the key's range
 *
 ******************************************************************/

w_rc_t data_access_histogram::update_access_count(const lpid_t& root, const Key& key, uint amount)
{
    w_rc_t r = RCOK;
    foo kv((char*)key._base[0].ptr,key._base[0].len,false);
    //sub_ranges_lock_iter sub_lock_iter; 

    // 1. acquire the locks if necessary
    if(!_is_local) {
	_histogram_lock.acquire_read();
	_root_locks[root].acquire_read();
	//W_DO(_acquire_lock(root, kv, sub_lock_iter, true));
    }

    // 2. update the range bucket
    ranges_hist_iter ranges_iter = _range_accesses.find(root);
    if(ranges_iter == _range_accesses.end()) {
	r = RC(hist_ROOT_DOESNT_EXIST);
    } else {
	sub_ranges_hist_iter sub_ranges_iter = (ranges_iter->second).lower_bound(kv);
	if(sub_ranges_iter == (ranges_iter->second).end()) {
	    r = RC(hist_RANGE_DOESNT_EXIST);
	} else {
	    atomic_add_int(&(sub_ranges_iter->second), amount);
	}
    }

    // 3. release the locks if necessary
    if(!_is_local) {
	//W_DO(_release_lock(sub_ranges_iter, true));
	_root_locks[root].release_read();
	_histogram_lock.release_read();
    }
    
    return (r);
}


/****************************************************************** 
 *
 * @fn:      add_bucket()
 *
 * @brief:   adds a new bucket for a new subroot
 *
 * @param:   lpid_t root    - 
 *
 ******************************************************************/

w_rc_t data_access_histogram::add_bucket(const lpid_t& root, int granularity)
{
    assert(0);
    // TODO:
    return (RCOK);
}


/****************************************************************** 
 *
 * @fn:      add_sub_bucket()
 *
 * @brief:   adds a new bucket for a new subroot
 *
 * @param:   lpid_t root    - 
 *
 ******************************************************************/

w_rc_t data_access_histogram::add_sub_bucket(const lpid_t& root, const Key& key, int amount)
{
    assert(0);
    // TODO:
    return (RCOK);
}


/****************************************************************** 
 *
 * @fn:      delete_bucket()
 *
 * @brief:   adds a new bucket for a new subroot
 *
 * @param:   lpid_t root    - 
 *
 ******************************************************************/

w_rc_t data_access_histogram::delete_bucket(const lpid_t& root)
{
    assert(0);
    // TODO:
    return (RCOK);
}


/****************************************************************** 
 *
 * @fn:      delete_sub_bucket()
 *
 * @brief:   adds a new bucket for a new subroot
 *
 * @param:   lpid_t root    - 
 *
 ******************************************************************/

w_rc_t data_access_histogram::delete_sub_bucket(const lpid_t& root, const Key& key)
{
    assert(0);
    // TODO:
    return (RCOK);
}




//// granularity map management  ////

/****************************************************************** 
 *
 * @fn:      update_granularity()
 *
 * @brief:   Updates the statistics collection granularity of the subtree
 *           given with root and rearranges the accesses map of that subtree
 *           based on the new granularity
 *
 * @param:   lpid_t root          -
 * @param:   int new_granularity  - 
 *
 ******************************************************************/
   
w_rc_t data_access_histogram::update_granularity(const lpid_t& root, int new_granularity)
{
    w_rc_t r = RCOK;

    // 1. acquire the locks if necessary
    if(!_is_local) {
	_histogram_lock.acquire_read();
	_root_locks[root].acquire_write();
    }

    // 2. update the range bucket
    gran_map_iter gran_iter = _granularities.find(root);
    if(gran_iter == _granularities.end()) {
	r = RC(hist_ROOT_DOESNT_EXIST);
    } else {
	gran_iter->second = new_granularity;
	assert(0);
	// TODO: update the subranges and distribute the numbers
	//       right now have a problem with updating the subranges
	//       see the non-default constructor for more expalantion
    }

    // 3. release the locks if necessary
    if(!_is_local) {
	_root_locks[root].release_write();
	_histogram_lock.release_read();
    }
    
    return (r);
}




// operator =

data_access_histogram& data_access_histogram::operator=(const data_access_histogram& rhs)
{
    // pin: since now we think of a central data_access_histogram we don't need this
    
    DBG(<<"Copying the data access histogram: ");
    /*    
    ranges_hist_citer ranges_citer;
    sub_ranges_hist_citer sub_ranges_citer;
    //ranges_locks_citer locks_citer;
    //sub_ranges_locks_citer sub_locks_citer;
    key_values_citer keys_citer;
    sub_key_values_citer sub_keys_citer;
    
    _histogram_lock.acquire_write();
    //rhs._histogram_lock.acquire_read();
    
    _foo_keys.clear();
    _range_accesses.clear();
    //_range_locks.clear();
    _granularities.clear();
    
    for(keys_citer = rhs._foo_keys.begin();
	keys_citer != rhs._foo_keys.end();
	keys_citer++) {
	for(sub_keys_citer = (keys_citer->second).begin();
	    sub_keys_citer != (keys_citer->second).end();
	    sub_keys_citer++) {
	    foo* newkv = new foo((*sub_keys_citer)->_m, (*sub_keys_citer)->_len, true);
	    
	    sub_ranges_citer = rhs._range_accesses[keys_citer->first].lower_bound(*(*sub_keys_citer));
	    assert(sub_ranges_citer != rhs._range_accesses[keys_citer->first].end());
	    _range_accesses[keys_citer->first][*newkv] = sub_ranges_citer->second;

	    //sub_locks_citer = rhs._range_locks[keys_citer->first].lower_bound(*(*sub_keys_citer));
	    //assert(sub_locks_citer != rhs._range_locks[keys_citer->first].end());
	    //_range_locks[keys_citer->first][*newkv] = sub_locks_citer->second;

	    _foo_keys[keys_citer->first].push_back(newkv);
	}

	_granularities[keys_citer->first] = rhs._granularities[keys_citer->first];
	assert(_granularities[keys_citer->first] != 0);

    }

    _is_local = rhs._is_local;

    //rhs._histogram_lock.release_read();
    _histogram_lock.release_write();
    */    
    return *this;
}
