#include "RowCache.h"

#include <algorithm>

namespace ecode
{
using namespace eacp;

bool RowCache::revalidate(const RowCacheStamp& stamp)
{
    if (stamp == current)
        return true;

    current = stamp;

    // Not clear(): an empty cache being "discarded" on the very first frame
    // would report a rebuild that never happened, and the counter is what the
    // tests read.
    if (rows.empty())
        return false;

    clear();

    return false;
}

void RowCache::setWindow(std::size_t first, std::size_t last)
{
    if (last <= first)
    {
        rows.clear();
        firstRow = 0;

        return;
    }

    auto moved = std::vector<std::optional<CachedRow>> {};
    moved.resize(last - first);

    // Whatever both windows hold keeps its layout; a scroll of one row would
    // otherwise throw away a screenful to move it by one place.
    const auto sharedFirst = std::max(first, firstRow);
    const auto sharedLast = std::min(last, firstRow + rows.size());

    for (auto row = sharedFirst; row < sharedLast; ++row)
        moved[row - first] = std::move(rows[row - firstRow]);

    rows = std::move(moved);
    firstRow = first;
}

const CachedRow* RowCache::find(std::size_t row) const
{
    if (row < firstRow || row - firstRow >= rows.size())
        return nullptr;

    const auto& entry = rows[row - firstRow];

    return entry ? &entry.value() : nullptr;
}

CachedRow* RowCache::store(std::size_t row, CachedRow&& entry)
{
    if (row < firstRow || row - firstRow >= rows.size())
        return nullptr;

    auto& slot = rows[row - firstRow];

    slot = std::move(entry);

    ++laidOut;

    return &slot.value();
}

void RowCache::clear()
{
    rows.clear();
    firstRow = 0;

    ++discarded;
}

std::size_t RowCache::rowsHeld() const
{
    return static_cast<std::size_t>(std::count_if(
        rows.begin(),
        rows.end(),
        [](const std::optional<CachedRow>& row) { return row.has_value(); }));
}
} // namespace ecode
