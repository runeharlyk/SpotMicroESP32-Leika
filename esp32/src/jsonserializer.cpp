#include <globals.h>
#include <SPIFFS.h>
#include <jsonserializer.h>
#include <taskmanager.h>

DRAM_ATTR std::unique_ptr<JSONWriter> g_ptrJSONWriter = nullptr;

bool LoadJSONFile(const char *fileName, size_t& bufferSize, std::unique_ptr<AllocatedJsonDocument>& pJsonDoc)
{
    bool jsonReadSuccessful = false;

    File file = SPIFFS.open(fileName);

    if (file)
    {
        if (file.size() > 0)
        {
            log_i("Attempting to read JSON file %s", fileName);

            if (bufferSize == 0)
                bufferSize = std::max((size_t)JSON_BUFFER_BASE_SIZE, file.size());

            // Loop is here to deal with out of memory conditions
            while(true)
            {
                pJsonDoc.reset(new AllocatedJsonDocument(bufferSize));

                DeserializationError error = deserializeJson(*pJsonDoc, file);

                if (error == DeserializationError::NoMemory)
                {
                    pJsonDoc.reset(nullptr);
                    file.seek(0);
                    bufferSize += JSON_BUFFER_INCREMENT;

                    log_w("Out of memory reading JSON from file %s - increasing buffer to %zu bytes", fileName, bufferSize);
                }
                else if (error == DeserializationError::Ok)
                {
                    jsonReadSuccessful = true;
                    break;
                }
                else
                {
                    log_w("Error with code %d occurred while deserializing JSON from file %s", to_value(error.code()), fileName);
                    break;
                }
            }
        }

        file.close();
    }

    return jsonReadSuccessful;
}

void SerializeWithBufferSize(std::unique_ptr<AllocatedJsonDocument>& pJsonDoc, size_t& bufferSize, std::function<bool(JsonObject&)> serializationFunction)
{
    // Loop is here to deal with out of memory conditions
    while(true)
    {
        pJsonDoc.reset(new AllocatedJsonDocument(bufferSize));
        JsonObject jsonObject = pJsonDoc->to<JsonObject>();

        if (serializationFunction(jsonObject))
            break;

        pJsonDoc.reset(nullptr);
        bufferSize += JSON_BUFFER_INCREMENT;

        log_w("Out of memory serializing object - increasing buffer to %zu bytes", bufferSize);
    }
}

bool SaveToJSONFile(const char *fileName, size_t& bufferSize, IJSONSerializable& object)
{
    if (bufferSize == 0)
        bufferSize = JSON_BUFFER_BASE_SIZE;

    std::unique_ptr<AllocatedJsonDocument> pJsonDoc(nullptr);

    SerializeWithBufferSize(pJsonDoc, bufferSize, [&object](JsonObject& jsonObject) { return object.SerializeToJSON(jsonObject); });

    SPIFFS.remove(fileName);

    File file = SPIFFS.open(fileName, FILE_WRITE);

    if (!file)
    {
        log_e("Unable to open file %s to write JSON!", fileName);
        return false;
    }

    size_t bytesWritten = serializeJson(*pJsonDoc, file);
    log_i("Number of bytes written to JSON file %s: %d", fileName, bytesWritten);

    file.flush();
    file.close();

    if (bytesWritten == 0)
    {
        log_e("Unable to write JSON to file %s!", fileName);
        SPIFFS.remove(fileName);
        return false;
    }

/*
    file = SPIFFS.open(fileName);
    if (file)
    {
        while (file.available())
            Serial.write(file.read());

        file.close();
    }
*/

    return true;
}

bool RemoveJSONFile(const char *fileName)
{
    return SPIFFS.remove(fileName);
}

size_t JSONWriter::RegisterWriter(std::function<void()> writer)
{
    // Add the writer with its flag unset
    writers.emplace_back(writer);
    return writers.size() - 1;
}

void JSONWriter::FlagWriter(size_t index)
{
    // Check if we received a valid writer index
    if (index >= writers.size())
        return;

    writers[index].flag = true;
    latestFlagMs = millis();

    g_TaskManager.NotifyJSONWriterThread();
}

// JSONWriterTaskEntry
//
// Invoke functions that write serialized JSON objects to SPIFFS at request, with some delay
void IRAM_ATTR JSONWriterTaskEntry(void *)
{
    for(;;)
    {
        TickType_t notifyWait = portMAX_DELAY;

        for (;;)
        {
            // Wait until we're woken up by a writer being flagged, or until we've reached the hold point
            ulTaskNotifyTake(pdTRUE, notifyWait);

            if (!g_ptrJSONWriter)
                continue;

            unsigned long holdUntil = g_ptrJSONWriter->latestFlagMs + JSON_WRITER_DELAY;
            unsigned long now = millis();
            if (now >= holdUntil)
                break;

            notifyWait = pdMS_TO_TICKS(holdUntil - now);
        }

        for (auto &entry : g_ptrJSONWriter->writers)
        {
            // Unset flag before we do the actual write. This makes that we don't miss another flag raise if it happens while writing
            if (entry.flag.exchange(false))
                entry.writer();
        }
    }
}
