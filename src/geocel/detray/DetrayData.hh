//------------------------------- -*- C++ -*- -------------------------------//
// Copyright Celeritas contributors: see top-level COPYRIGHT file for details
// SPDX-License-Identifier: (Apache-2.0 OR MIT)
//---------------------------------------------------------------------------//
//! \file geocel/detray/DetrayData.hh
//---------------------------------------------------------------------------//
#pragma once

#include "corecel/Config.hh"

#include "corecel/Macros.hh"
#include "corecel/Types.hh"
#include "corecel/data/Collection.hh"
#include "corecel/data/CollectionBuilder.hh"
#include "corecel/sys/ThreadId.hh"
#include "geocel/Types.hh"

namespace celeritas
{
//---------------------------------------------------------------------------//
// PARAMS
//---------------------------------------------------------------------------//
/*!
 * Persistent data used by Detray implementation.
 */
template<Ownership W, MemSpace M>
struct DetrayParamsData
{
    //! Whether the interface is initialized
    explicit CELER_FUNCTION operator bool() const { return true; }

    //! Assign from another set of data
    template<Ownership W2, MemSpace M2>
    DetrayParamsData& operator=(DetrayParamsData<W2, M2>& other)
    {
        static_assert(M2 == M && W2 == Ownership::value
                          && W == Ownership::reference,
                      "Only supported assignment is from value to reference");
        CELER_EXPECT(other);
        return *this;
    }
};

//---------------------------------------------------------------------------//
// STATE
//---------------------------------------------------------------------------//
/*!
 * Interface for Detray state information.
 */
template<Ownership W, MemSpace M>
struct DetrayStateData
{
    //// TYPES ////

    template<class T>
    using Items = celeritas::StateCollection<T, W, M>;

    //// DATA ////

    // Collections
    Items<Real3> pos;
    Items<Real3> dir;

    //// METHODS ////

    //! True if sizes are consistent and states are assigned
    explicit CELER_FUNCTION operator bool() const
    {
        return this->size() > 0 && dir.size() == this->size();
    }

    //! State size
    CELER_FUNCTION TrackSlotId::size_type size() const { return pos.size(); }

    //! Assign from another set of data
    template<Ownership W2, MemSpace M2>
    DetrayStateData& operator=(DetrayStateData<W2, M2>& other)
    {
        static_assert(M2 == M && W == Ownership::reference,
                      "Only supported assignment is from the same memspace to "
                      "a reference");
        CELER_EXPECT(other);
        pos = other.pos;
        dir = other.dir;
        return *this;
    }
};

//---------------------------------------------------------------------------//
/*!
 * Resize geometry states.
 */
template<MemSpace M>
void resize(DetrayStateData<Ownership::value, M>* data,
            HostCRef<DetrayParamsData> const&,
            size_type size)
{
    CELER_EXPECT(data);
    CELER_EXPECT(size > 0);

    resize(&data->pos, size);
    resize(&data->dir, size);

    CELER_ENSURE(data);
}
//---------------------------------------------------------------------------//
}  // namespace celeritas
