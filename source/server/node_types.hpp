//  node types
//  Copyright (C) 2008, 2009 Tim Blechmann
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; see the file COPYING.  If not, write to
//  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
//  Boston, MA 02111-1307, USA.

#ifndef SERVER_NODE_TYPES_HPP
#define SERVER_NODE_TYPES_HPP

#include <boost/detail/atomic_count.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive_ptr.hpp>

#include "synth_prototype.hpp"
#include "utilities/static_pool.hpp"

namespace nova
{

class server_node;

class abstract_group;
class group;

namespace bi = boost::intrusive;

typedef boost::intrusive::list<class server_node,
                                boost::intrusive::constant_time_size<false> >
server_node_list;

class server_node:
    public bi::list_base_hook<bi::link_mode<bi::auto_unlink> >, /* group member */
    public bi::set_base_hook<bi::link_mode<bi::auto_unlink> >  /* for node_id mapping */
{
protected:
    server_node(uint32_t node_id, bool type):
        node_id(node_id), synth_(type), running_(true), parent_(0), use_count_(0)
    {}

    virtual ~server_node(void)
    {
        assert(parent_ == 0);
    }


    /* @{ */
    /** node id handling */
    void reset_id(uint32_t new_id)
    {
        node_id = new_id;
    }

    uint32_t node_id;

public:
    uint32_t id(void) const
    {
        return node_id;
    }
    /* @} */

    typedef bi::list_base_hook<bi::link_mode<bi::auto_unlink> > parent_hook;

    /* @{ */
    /** node_id mapping */
    friend bool operator< (server_node const & lhs, server_node const & rhs)
    {
        return lhs.node_id < rhs.node_id;
    }
    friend bool operator== (server_node const & lhs, server_node const & rhs)
    {
        return lhs.node_id == rhs.node_id;
    }
    /* @} */

    bool is_synth(void) const
    {
        return synth_;
    }

    /** set a slot */
    /* @{ */
    virtual void set(const char * slot_str, float val) = 0;
    virtual void set(const char * slot_str, size_t n, float * values) = 0;
    virtual void set(slot_index_t slot_id, float val) = 0;
    virtual void set(slot_index_t slot_str, size_t n, float * values) = 0;
    /* @} */


    /* @{ */
    /* group traversing */
    inline server_node * previous_node(void);
    inline server_node * next_node(void);
    /* @} */

private:
    bool synth_;

    /** support for pausing node */
    /* @{ */
    bool running_;

public:
    virtual void pause(void)
    {
        running_ = false;
    }

    virtual void resume(void)
    {
        running_ = true;
    }

    bool is_running(void) const
    {
        return running_;
    }
    /* @} */

private:
    friend class node_graph;
    friend class abstract_group;
    friend class group;
    friend class parallel_group;


public:
    const abstract_group * get_parent(void) const
    {
        return parent_;
    }

    abstract_group * get_parent(void)
    {
        return parent_;
    }

    void set_parent(abstract_group * parent)
    {
        add_ref();
        assert(parent_ == 0);
        parent_ = parent;
    }

    void clear_parent(void)
    {
        parent_ = 0;
        release();
    }

    abstract_group * parent_;

public:
    /* memory management for server_nodes */
    /* @{ */
    template<typename T>
    static T * allocate(std::size_t count)
    {
        return static_cast<T*>(allocate(count * sizeof(T)));
    }

    static void * allocate(std::size_t size);
    static void free(void *);
    static std::size_t get_max_size(void)
    {
        return pool.get_max_size();
    }

    inline void * operator new(std::size_t size)
    {
        return allocate(size);
    }

    inline void operator delete(void * p)
    {
        free(p);
    }

private:
    typedef static_pool<1024*1024*8> node_pool;
    static node_pool pool;
    /* @} */

public:
    /* refcountable */
    /* @{ */
    void add_ref(void)
    {
        ++use_count_;
    }

    void release(void)
    {
        if(unlikely(--use_count_ == 0))
            delete this;
    }

private:
    boost::detail::atomic_count use_count_;
    /* @} */
};

inline void intrusive_ptr_add_ref(server_node * p)
{
    p->add_ref();
}

inline void intrusive_ptr_release(server_node * p)
{
    p->release();
}

typedef boost::intrusive_ptr<server_node> server_node_ptr;
typedef boost::intrusive_ptr<class synth> synth_ptr;
typedef boost::intrusive_ptr<group> group_ptr;

enum node_position
{
    head = 0,
    tail = 1,
    before = 2,
    after = 3,
    replace = 4,
    insert = 5                  /* for pgroups */
};

typedef std::pair<server_node *, node_position> node_position_constraint;


template <typename synth_t>
inline synth_t * synth_prototype::allocate(void)
{
    return static_cast<synth_t*>(server_node::allocate(sizeof(synth_t)));
}



/* allocator class, using server_node specific memory pool */
template <class T>
class server_node_allocator
{
public:
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;
    typedef T*        pointer;
    typedef const T*  const_pointer;
    typedef T&        reference;
    typedef const T&  const_reference;
    typedef T         value_type;

    template <class U> struct rebind
    {
        typedef server_node_allocator<U> other;
    };

    server_node_allocator(void) throw()
    {}

    ~server_node_allocator() throw()
    {}

    pointer address(reference x) const
    {
        return &x;
    }

    const_pointer address(const_reference x) const
    {
        return &x;
    }

    pointer allocate(size_type n,
                     const_pointer hint = 0)
    {
        pointer ret = static_cast<pointer>(server_node::allocate(n * sizeof(T)));
        if (unlikely(ret == 0))
            throw std::bad_alloc();

        return ret;
    }

    void deallocate(pointer p, size_type n)
    {
        server_node::free(p);
    }

    size_type max_size() const throw()
    {
        return server_node::get_max_size();
    }

    void construct(pointer p, const T& val)
    {
        ::new(p) T(val);
    }

    void destroy(pointer p)
    {
        p->~T();
    }
};


template<typename T, typename U>
bool operator==( server_node_allocator<T> const& left, server_node_allocator<U> const& right )
{
    return !(left != right);
}

template<typename T, typename U>
bool operator!=( server_node_allocator<T> const& left, server_node_allocator<U> const& right )
{
    return true;
}


} /* namespace nova */

#endif /* SERVER_NODE_TYPES_HPP */
