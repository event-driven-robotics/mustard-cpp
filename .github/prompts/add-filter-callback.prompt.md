---
description: "Scaffold a new processing callback function for the DataLoader<T> callback pipeline. Provide the loader type and what the callback should do."
agent: agent
tools: [read, edit, search]
argument-hint: "Loader type and callback purpose (e.g. BinaryEventLoader: filter events below y=100)"
---
Scaffold a new callback for the `DataLoader<T>` callback pipeline based on: `$ARGUMENTS`

## Steps
1. Read `include/mustard/data/DataLoader.h` to understand the `addCallback(std::function<void(DataChunk<T>&)>)` signature
2. Read the relevant loader header to identify the concrete type `T`
3. Implement the callback as a free function or lambda:
   ```cpp
   // Free function style (preferred for reusability):
   void filterBelowY(DataChunk<DVSEvent>& chunk) {
       chunk.data.erase(
           std::remove_if(chunk.data.begin(), chunk.data.end(),
               [](const DVSEvent& e) { return e.y < 100; }),
           chunk.data.end());
   }
   ```
4. Show how to register the callback on the loader instance:
   ```cpp
   loader.addCallback(filterBelowY);
   // or with a lambda:
   loader.addCallback([threshold](DataChunk<DVSEvent>& chunk) {
       // ...
   });
   ```
5. Write a short unit test (no file I/O, synthetic data) verifying the callback is applied and the expected events are retained/removed

## Output
- The callback implementation (free function preferred)
- The registration snippet
- A minimal GoogleTest test case for the callback
- Note any const-correctness or performance considerations (e.g., avoid copying the whole chunk)
