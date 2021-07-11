//
// Copyright (c) 2017-2020 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#pragma once

#include <EASTL/iterator.h>
#include <EASTL/vector.h>
#include <EASTL/utility.h>

namespace Urho3D
{

/// Vector of vectors.
template <class T>
class MultiVector
{
public:
    /// Inner collection type.
    using InnerCollection = ea::vector<T>;
    /// Outer collection type.
    using OuterCollection = ea::vector<InnerCollection>;
    /// Index in multi-vector (pair of outer and inner indices).
    using Index = ea::pair<unsigned, unsigned>;

    /// Iterator base.
    template <class OuterIterator, class InnerIterator>
    class BaseIterator : public ea::iterator<EASTL_ITC_NS::forward_iterator_tag, T>
    {
    public:
        /// Construct default.
        BaseIterator() = default;

        /// Construct from outer range.
        BaseIterator(OuterIterator begin, OuterIterator end)
            : outer_(begin)
            , outerEnd_(end)
        {
            while (outer_ != outerEnd_ && outer_->empty())
                ++outer_;
            if (outer_ != outerEnd_)
            {
                inner_ = outer_->begin();
                innerEnd_ = outer_->end();
            }
        }

        /// Dereference.
        auto&& operator*() const { return *inner_; }

        /// Dereference.
        auto operator->() const { return &*inner_; }

        /// Advance by N.
        BaseIterator& operator+=(unsigned diff)
        {
            while (diff != 0)
            {
                // Advance inner iterator as much as possible
                const unsigned maxDiff = ea::min(diff, static_cast<unsigned>(innerEnd_ - inner_));
                inner_ += maxDiff;
                diff -= maxDiff;

                // Advance outer iterator if necessary
                if (inner_ == innerEnd_)
                    fixInnerIterator();
            }
            return *this;
        }

        /// Advance by N.
        BaseIterator operator+(unsigned diff) const
        {
            auto iter = *this;
            iter += diff;
            return iter;
        }

        /// Pre-increment.
        BaseIterator& operator++()
        {
            ++inner_;
            if (inner_ == innerEnd_)
                fixInnerIterator();
            return *this;
        }

        /// Post-increment.
        BaseIterator operator++(int)
        {
            auto iter = *this;
            ++*this;
            return iter;
        }

        /// Compare equal.
        bool operator==(const BaseIterator& rhs) const
        {
            return outer_ == rhs.outer_ && inner_ == rhs.inner_;
        }

        /// Compare not equal.
        bool operator!=(const BaseIterator& rhs) const { return !(*this == rhs); }

    private:
        /// Fix inner iterator when it reaches end.
        void fixInnerIterator()
        {
            do ++outer_;
            while (outer_ != outerEnd_ && outer_->empty());

            if (outer_ != outerEnd_)
            {
                inner_ = outer_->begin();
                innerEnd_ = outer_->end();
            }
            else
            {
                inner_ = InnerIterator{};
                innerEnd_ = InnerIterator{};
            }
        }

        /// Outer iterator (current).
        OuterIterator outer_{};
        /// Outer iterator (end).
        OuterIterator outerEnd_{};
        /// Inner iterator (current).
        InnerIterator inner_{};
        /// Inner iterator (end).
        InnerIterator innerEnd_{};
    };

    /// Mutable iterator.
    using Iterator = BaseIterator<typename OuterCollection::iterator, typename InnerCollection::iterator>;
    /// Const iterator.
    using ConstIterator = BaseIterator<typename OuterCollection::const_iterator, typename InnerCollection::const_iterator>;

    /// Clear inner vectors. Reset outer vector to fixed size.
    void Clear(unsigned outerSize)
    {
        outer_.resize(outerSize);
        for (auto& inner : outer_)
            inner.clear();
    }

    /// Emplace element at the back of specified outer vector.
    template <class ... Args>
    T& EmplaceBack(unsigned outerIndex, Args&& ... args)
    {
        auto& inner = outer_[outerIndex];
        return inner.emplace_back(std::forward<Args>(args)...);
    }

    /// Push element into back of specified outer vector. Return index of added element.
    Index PushBack(unsigned outerIndex, const T& value)
    {
        auto& inner = outer_[outerIndex];
        const unsigned innerIndex = inner.size();
        inner.push_back(value);
        return { outerIndex, innerIndex };
    }

    /// Return size.
    unsigned Size() const
    {
        unsigned size = 0;
        for (auto& inner : outer_)
            size += inner.size();
        return size;
    }

    /// Copy content to vector.
    void CopyTo(InnerCollection& dest) const
    {
        dest.clear();
        for (const auto& inner : outer_)
            dest.append(inner);
    }

    /// Return element (mutable).
    T& operator[](const Index& index) { return outer_[index.first][index.second]; }

    /// Return element (mutable).
    const T& operator[](const Index& index) const { return outer_[index.first][index.second]; }

    /// Return outer collection (mutable).
    const OuterCollection& GetUnderlyingCollection() const { return outer_; }

    /// Return outer collection (const).
    OuterCollection& GetUnderlyingCollection() { return outer_; }

    /// Return begin iterator (mutable).
    Iterator Begin() { return Iterator{ outer_.begin(), outer_.end() }; }

    /// Return end iterator (mutable).
    Iterator End() { return Iterator{ outer_.end(), outer_.end() }; }

    /// Return begin iterator (const).
    ConstIterator Begin() const { return ConstIterator{ outer_.begin(), outer_.end() }; }

    /// Return end iterator (const).
    ConstIterator End() const { return ConstIterator{ outer_.end(), outer_.end() }; }

private:
    /// Internal collection.
    OuterCollection outer_;
};

/// Return begin iterator of const MultiVector.
template <class T> auto begin(const MultiVector<T>& c) { return c.Begin(); }
/// Return end iterator of const MultiVector.
template <class T> auto end(const MultiVector<T>& c) { return c.End(); }
/// Return begin iterator of mutable MultiVector.
template <class T> auto begin(MultiVector<T>& c) { return c.Begin(); }
/// Return end iterator of mutable MultiVector.
template <class T> auto end(MultiVector<T>& c) { return c.End(); }
/// Return size of MultiVector.
template <class T> unsigned size(const MultiVector<T>& c) { return c.Size(); }

}
