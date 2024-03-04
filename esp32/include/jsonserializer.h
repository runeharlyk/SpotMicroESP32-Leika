#pragma once

#include <atomic>
#include <ArduinoJson.h>
#include <jsonbase.h>

struct IJSONSerializable
{
    virtual bool SerializeToJSON(JsonObject& jsonObject) = 0;
    virtual bool DeserializeFromJSON(const JsonObjectConst& jsonObject) { return false; }
};

template <class E>
constexpr auto to_value(E e) noexcept
{
	return static_cast<std::underlying_type_t<E>>(e);
}

#if USE_PSRAM
    struct JsonPsramAllocator
    {
        void* allocate(size_t size) {
            return ps_malloc(size);
        }

        void deallocate(void* pointer) {
            free(pointer);
        }

        void* reallocate(void* ptr, size_t new_size) {
            return ps_realloc(ptr, new_size);
        }
    };

    typedef BasicJsonDocument<JsonPsramAllocator> AllocatedJsonDocument;

#else
    typedef DynamicJsonDocument AllocatedJsonDocument;
#endif

bool LoadJSONFile(const char *fileName, size_t& bufferSize, std::unique_ptr<AllocatedJsonDocument>& pJsonDoc);
bool SaveToJSONFile(const char *fileName, size_t& bufferSize, IJSONSerializable& object);
bool RemoveJSONFile(const char *fileName);

#define JSON_WRITER_DELAY 3000

class JSONWriter
{
    // We allow the main JSON Writer task entry point function to access private members
    friend void IRAM_ATTR JSONWriterTaskEntry(void *);

  private:

    // Writer function and flag combo
    struct WriterEntry
    {
        std::atomic_bool flag = false;
        std::function<void()> writer;

        WriterEntry(std::function<void()> writer) :
            writer(writer)
        {}

        WriterEntry(WriterEntry&& entry) : WriterEntry(entry.writer)
        {}
    };

    std::vector<WriterEntry> writers;
    std::atomic_ulong latestFlagMs;

  public:

    // Add a writer to the collection. Returns the index of the added writer, for use with FlagWriter()
    size_t RegisterWriter(std::function<void()> writer);

    // Flag a writer for invocation and wake up the task that calls them
    void FlagWriter(size_t index);
};

extern DRAM_ATTR std::unique_ptr<JSONWriter> g_ptrJSONWriter;
