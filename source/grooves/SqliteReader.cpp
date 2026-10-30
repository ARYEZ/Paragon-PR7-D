#include "grooves/SqliteReader.h"

namespace grooves::sqlite
{
    namespace
    {
        juce::uint32 be16 (const juce::uint8* p) noexcept { return (juce::uint32) ((p[0] << 8) | p[1]); }
        juce::uint32 be32 (const juce::uint8* p) noexcept
        {
            return ((juce::uint32) p[0] << 24) | ((juce::uint32) p[1] << 16) | ((juce::uint32) p[2] << 8) | (juce::uint32) p[3];
        }

        // A varint (1 to 9 bytes, big-endian, 7 bits a byte, the 9th all 8): how many bytes it took, 0 if it ran off
        // the end.
        int varint (const juce::uint8* p, const juce::uint8* end, juce::uint64& value) noexcept
        {
            value = 0;
            for (int i = 0; i < 9; ++i)
            {
                if (p + i >= end)
                    return 0;
                if (i == 8)
                {
                    value = (value << 8) | p[i];
                    return 9;
                }
                value = (value << 7) | (juce::uint64) (p[i] & 0x7f);
                if ((p[i] & 0x80) == 0)
                    return i + 1;
            }
            return 0;
        }

        // A record (the header's serial types, then the values): false if it doesn't fit its bytes.
        bool decodeRecord (const juce::uint8* p, size_t n, std::vector<Value>& out)
        {
            const auto* end = p + n;
            juce::uint64 headerSize = 0;
            const auto first = varint (p, end, headerSize);
            if (first == 0 || headerSize > n || headerSize < (juce::uint64) first)
                return false;
            auto h = (size_t) first;
            auto body = (size_t) headerSize;
            while (h < headerSize)
            {
                juce::uint64 type = 0;
                const auto used = varint (p + h, p + headerSize, type);
                if (used == 0)
                    return false;
                h += (size_t) used;
                Value v;
                size_t length = 0;
                if (type >= 1 && type <= 6)
                    length = (size_t) std::array<int, 7> { 0, 1, 2, 3, 4, 6, 8 }[(size_t) type];
                else if (type == 7)
                    length = 8;
                else if (type == 10 || type == 11)
                    return false;   // reserved
                else if (type >= 12)
                    length = (size_t) ((type - (type % 2 == 0 ? 12 : 13)) / 2);
                if (length > n || body + length > n)
                    return false;
                const auto* at = p + body;
                if (type >= 1 && type <= 6)
                {
                    v.type = Value::Type::integer;
                    juce::int64 x = (at[0] & 0x80) != 0 ? -1 : 0;   // sign-extended
                    for (size_t i = 0; i < length; ++i)
                        x = (juce::int64) (((juce::uint64) x << 8) | at[i]);
                    v.integer = x;
                }
                else if (type == 7)
                {
                    juce::uint64 bits = 0;
                    for (int i = 0; i < 8; ++i)
                        bits = (bits << 8) | at[i];
                    std::memcpy (&v.real, &bits, sizeof (double));
                    v.type = Value::Type::real;
                }
                else if (type == 8 || type == 9)
                {
                    v.type = Value::Type::integer;
                    v.integer = type == 9 ? 1 : 0;
                }
                else if (type >= 12 && type % 2 == 0)
                {
                    v.type = Value::Type::blob;
                    v.blob = juce::MemoryBlock (at, length);
                }
                else if (type >= 13)
                {
                    v.type = Value::Type::text;
                    v.text = juce::String::fromUTF8 (reinterpret_cast<const char*> (at), (int) length);
                }
                body += length;
                out.push_back (std::move (v));
            }
            return true;
        }

        juce::String unquote (juce::String s)
        {
            return s.trim().trimCharactersAtStart ("\"'`[").trimCharactersAtEnd ("\"'`]");
        }

        // A table's columns from its CREATE TABLE statement, and which one (if any) is its INTEGER PRIMARY KEY, which
        // SQLite keeps as the rowid.
        void parseColumns (const juce::String& sql, juce::StringArray& columns, int& rowidColumn)
        {
            const auto open = sql.indexOfChar ('('), close = sql.lastIndexOfChar (')');
            if (open < 0 || close <= open)
                return;
            const auto body = sql.substring (open + 1, close);
            juce::StringArray items;
            auto depth = 0, start = 0;
            for (int i = 0; i < body.length(); ++i)
            {
                const auto c = body[i];
                if (c == '(')
                    ++depth;
                else if (c == ')')
                    --depth;
                else if (c == ',' && depth == 0)
                {
                    items.add (body.substring (start, i));
                    start = i + 1;
                }
            }
            items.add (body.substring (start));

            juce::StringArray types;
            juce::String key;
            for (const auto& raw : items)
            {
                const auto item = raw.replaceCharacters ("\t\r\n", "   ").trim();
                const auto words = juce::StringArray::fromTokens (item, " ", "\"'`");
                if (words.isEmpty())
                    continue;
                const auto head = words[0].toUpperCase();
                const auto upper = item.toUpperCase();
                if (head == "CONSTRAINT" || head == "PRIMARY" || head == "UNIQUE" || head == "FOREIGN" || head == "CHECK")
                {
                    if (const auto at = upper.indexOf ("PRIMARY KEY"); at >= 0)
                    {
                        const auto inside = item.substring (at).fromFirstOccurrenceOf ("(", false, false).upToFirstOccurrenceOf (")", false, false);
                        if (! inside.containsChar (','))
                            key = unquote (inside);
                    }
                    continue;
                }
                columns.add (unquote (words[0]));
                types.add (words.size() > 1 ? words[1].toUpperCase() : juce::String());
                if (upper.contains ("PRIMARY KEY"))
                    key = unquote (words[0]);
            }
            if (key.isNotEmpty())
                if (const auto k = columns.indexOf (key, true); k >= 0 && types[k] == "INTEGER")
                    rowidColumn = k;
        }
    }

    bool Database::open (const juce::File& f)
    {
        if (! f.existsAsFile() || f.getSize() > 512 * 1024 * 1024)
            return false;
        juce::MemoryBlock data;
        return f.loadFileAsData (data) && open (std::move (data));
    }

    bool Database::open (juce::MemoryBlock data)
    {
        file = std::move (data);
        tables.clear();
        const auto* d = static_cast<const juce::uint8*> (file.getData());
        if (file.getSize() < 100 || std::memcmp (d, "SQLite format 3\0", 16) != 0)
            return false;
        pageSize = be16 (d + 16);
        if (pageSize == 1)
            pageSize = 65536;
        if (pageSize < 512 || pageSize > 65536 || (pageSize & (pageSize - 1)) != 0)
            return false;
        usable = pageSize - d[20];
        if (usable < 480)
            return false;
        if (const auto encoding = be32 (d + 56); encoding > 1)
            return false;   // UTF-16 text: not what groove databases use
        numPages = (juce::uint32) (file.getSize() / pageSize);

        // The schema: the table on page 1 (type, name, tbl_name, rootpage, sql).
        auto visits = 0;
        walk (1, 0, [this] (juce::int64, const std::vector<Value>& row)
        {
            if (row.size() < 5 || row[0].text != "table")
                return;
            Table t;
            t.name = row[1].text;
            t.root = (juce::uint32) row[3].asInt();
            if (row[4].text.toUpperCase().contains ("WITHOUT ROWID"))
                return;   // kept in another kind of tree: not read
            parseColumns (row[4].text, t.columns, t.rowidColumn);
            tables.push_back (std::move (t));
        }, visits);
        return true;
    }

    const juce::uint8* Database::pageData (juce::uint32 page) const noexcept
    {
        if (page == 0 || page > numPages)
            return nullptr;
        return static_cast<const juce::uint8*> (file.getData()) + (size_t) (page - 1) * pageSize;
    }

    const Database::Table* Database::find (const juce::String& name) const
    {
        for (const auto& t : tables)
            if (t.name.equalsIgnoreCase (name))
                return &t;
        return nullptr;
    }

    juce::StringArray Database::columnsOf (const juce::String& table) const
    {
        const auto* t = find (table);
        return t != nullptr ? t->columns : juce::StringArray();
    }

    bool Database::forEachRow (const juce::String& table, const std::function<void (juce::int64, const std::vector<Value>&)>& fn) const
    {
        const auto* t = find (table);
        if (t == nullptr)
            return false;
        auto visits = 0;
        walk (t->root, 0, [t, &fn] (juce::int64 rowid, const std::vector<Value>& values)
        {
            auto row = values;
            if (row.size() < (size_t) t->columns.size())
                row.resize ((size_t) t->columns.size());
            if (t->rowidColumn >= 0 && row[(size_t) t->rowidColumn].type == Value::Type::null)
            {
                row[(size_t) t->rowidColumn].type = Value::Type::integer;
                row[(size_t) t->rowidColumn].integer = rowid;
            }
            fn (rowid, row);
        }, visits);
        return true;
    }

    // A cell's payload: `size` bytes, the first part on its page at `at` (an offset into the file, the page ending at
    // `end`), the rest along the overflow pages.
    bool Database::payload (size_t at, size_t end, juce::uint64 size, juce::MemoryBlock& out) const
    {
        if (size > file.getSize())
            return false;
        const auto maxLocal = usable - 35;
        auto local = (size_t) size;
        auto overflows = false;
        if (size > maxLocal)
        {
            const auto minLocal = ((usable - 12) * 32 / 255) - 23;
            const auto k = minLocal + (size_t) ((size - minLocal) % (usable - 4));
            local = k <= maxLocal ? k : minLocal;
            overflows = true;
        }
        if (at + local + (overflows ? 4 : 0) > end)
            return false;
        const auto* d = static_cast<const juce::uint8*> (file.getData());
        out.setSize ((size_t) size);
        auto* dest = static_cast<juce::uint8*> (out.getData());
        std::memcpy (dest, d + at, local);
        if (! overflows)
            return true;
        auto done = local;
        auto next = be32 (d + at + local);
        for (juce::uint32 hops = 0; done < size && hops <= numPages; ++hops)
        {
            const auto* page = pageData (next);
            if (page == nullptr)
                return false;
            const auto take = juce::jmin ((size_t) size - done, usable - 4);
            std::memcpy (dest + done, page + 4, take);
            done += take;
            next = be32 (page);
        }
        return done == size;
    }

    bool Database::walk (juce::uint32 page, int depth, const std::function<void (juce::int64, const std::vector<Value>&)>& fn, int& visits) const
    {
        const auto* d = pageData (page);
        if (d == nullptr || depth > 40 || ++visits > (int) numPages + 1)
            return false;   // no such page, or a loop
        const size_t header = page == 1 ? 100 : 0;
        const auto type = d[header];
        if (type != 0x05 && type != 0x0d)
            return false;   // not a table's page
        const auto interior = type == 0x05;
        const size_t headerLength = interior ? 12 : 8;
        const auto cells = be16 (d + header + 3);
        if (header + headerLength + cells * 2 > usable)
            return false;
        const auto pageStart = (size_t) (page - 1) * pageSize;
        for (juce::uint32 c = 0; c < cells; ++c)
        {
            const size_t at = be16 (d + header + headerLength + 2 * c);
            if (at < header + headerLength || at >= usable)
                continue;
            if (interior)
            {
                if (at + 4 <= usable)
                    walk (be32 (d + at), depth + 1, fn, visits);
                continue;
            }
            juce::uint64 size = 0, rowid = 0;
            const auto a = varint (d + at, d + usable, size);
            if (a == 0)
                continue;
            const auto b = varint (d + at + a, d + usable, rowid);
            if (b == 0)
                continue;
            juce::MemoryBlock bytes;
            if (! payload (pageStart + at + (size_t) (a + b), pageStart + usable, size, bytes))
                continue;
            std::vector<Value> values;
            if (decodeRecord (static_cast<const juce::uint8*> (bytes.getData()), bytes.getSize(), values))
                fn ((juce::int64) rowid, values);
        }
        if (interior)
            walk (be32 (d + header + 8), depth + 1, fn, visits);
        return true;
    }
}
