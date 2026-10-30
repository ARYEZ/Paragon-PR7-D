#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

// A small, read-only reader for SQLite 3 database files (0.15.0), enough to read whole tables: the groove databases
// some drum software keeps its MIDI grooves in (see GrooveDatabase.h). It follows the published file format
// (sqlite.org/fileformat.html): the header, table b-trees (interior and leaf pages), records, overflow pages. It reads
// the main file only (a write-ahead log beside it isn't applied) and never writes. Every read is bounds-checked: a
// damaged or truncated file gives fewer rows or none, never a crash.
namespace grooves::sqlite
{
    struct Value
    {
        enum class Type { null, integer, real, text, blob };
        Type type = Type::null;
        juce::int64 integer = 0;
        double real = 0.0;
        juce::String text;
        juce::MemoryBlock blob;

        juce::int64 asInt() const noexcept { return type == Type::real ? (juce::int64) real : integer; }
        double asDouble() const noexcept { return type == Type::integer ? (double) integer : real; }
    };

    class Database
    {
    public:
        // Reads the file into memory (groove databases are a few megabytes at most): false if it isn't an SQLite 3
        // database in UTF-8 we can read.
        bool open (const juce::File&);
        bool open (juce::MemoryBlock data);

        // A table's columns, in order (from its CREATE TABLE statement); empty if there's no such table.
        juce::StringArray columnsOf (const juce::String& table) const;

        // Every row of a table: its rowid and its values in the table's column order (rows written before a column
        // was added have fewer: the rest are NULL; an INTEGER PRIMARY KEY column holds the rowid). False if there's no
        // such table.
        bool forEachRow (const juce::String& table, const std::function<void (juce::int64 rowid, const std::vector<Value>&)>&) const;

    private:
        struct Table { juce::String name; juce::uint32 root = 0; juce::StringArray columns; int rowidColumn = -1; };
        const Table* find (const juce::String& name) const;
        bool walk (juce::uint32 page, int depth, const std::function<void (juce::int64, const std::vector<Value>&)>&, int& visits) const;
        bool payload (size_t at, size_t end, juce::uint64 size, juce::MemoryBlock& out) const;
        const juce::uint8* pageData (juce::uint32 page) const noexcept;

        juce::MemoryBlock file;
        size_t pageSize = 0, usable = 0;
        juce::uint32 numPages = 0;
        std::vector<Table> tables;
    };
}
