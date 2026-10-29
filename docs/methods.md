# Methods, contracts, and correctness

## Graph and root model

N is the identifier-universe size, V the valid identifiers in `[0,N)`, and A the directed adjacency arcs among V. Invalid IDs occupy array slots but cannot be roots or visited vertices. Both degrees are at most four. Incoming/outgoing lists must be stable and mutually consistent.

In MEGAHIT the universe comprises stored SDBG edge IDs; traversal uses consecutive-edge adjacency. Report N, valid vertices, and arcs separately. Single-source runs require a valid source. Forests scan valid IDs in ascending order and start roots only at undiscovered IDs. In a directed graph, root count is not a weak/strong component count. Forest BFS distances restart at zero for each root; they are not distances from a common source.

## BFS

```text
clear visited bitmap
for requested root, or each undiscovered valid root in ascending order:
    mark root; queue = [root]; distance = 0
    while queue is nonempty:
        remaining = queue.size()
        repeat remaining times:
            v = queue.pop_front()
            emit B(v, distance)
            for valid outgoing neighbor w in oracle order:
                if w is unvisited: mark w; queue.push_back(w)
        increment distance
```

Marking on insertion discovers each vertex once. Inductively, unvisited neighbors of level d have distance d+1, giving shortest-path distances in single-source runs. FIFO level counters preserve the same insertion and output order.

The bitmap uses `8*ceil(N/64)` bytes; the queue requires O(W) identifiers, where W is maximum level width. At most the unprocessed current layer and the discovered next layer are pending. Auxiliary space is **O(N+W log N) bits** in the word-RAM model. Bounded degree does not imply narrow frontiers. Fixed b-bit IDs contribute b times the queue population before capacity/allocator effects. Peak requested allocation also includes container bookkeeping and temporary allocation overlap. Time is O(N+|A|) for a forest plus navigation cost; single-source work includes array initialization and reachable adjacency.

`bfs-base` stores 32-bit distances and a FIFO. `bfs-stream` uses a bitmap, FIFO, and level counters. A consumer needing persistent random-access distances must store them separately. Avoid attributing savings against distance-array BFS to algorithmic novelty. The two-frontier implementations are excluded from the paper and default experiment set.

## Predecessor-index DFS

```text
clear two-bit colors; allocate two-bit predecessor indices
for requested root, or each white valid root in ascending order:
    current = root; color active; emit D(root, NO_PARENT)
    repeat:
        child = first white valid outgoing neighbor of current
        if child exists:
            p = current's index in incoming(child)
            predecessor_index[child] = p
            color child active; emit D(child, current); current = child
        else:
            color current done; emit F(current)
            if current == root: finish this tree
            current = incoming(current)[predecessor_index[current]]
```

The index lies in `[0,3]`; stable incoming order recovers the parent without any distinct-label assumption. Missing parents are oracle-contract errors and must fail loudly.

At discovery the active chain equals the recursive DFS stack. A finished vertex has no white outgoing neighbor. Recovery returns to its active parent, whose next white child matches ordinary DFS under the same ordering. Active/done colors prevent rediscovery on cycles/self-loops. Re-scanning lists is linear under bounded degree. If backward navigation costs O(k), include O(k) per predecessor lookup/recovery in runtime analysis.

Two packed arrays occupy `16*ceil(N/32)` bytes: 4N payload bits plus rounding, with constant local words. This bounded-degree reverse-adjacency implementation is not the general EHK stack-restoration algorithm. The packed color array provides constant-time get/set; choice/iteration scan the array and do not provide Kammer-Sajenko complexity guarantees.

Packed-color explicit DFS uses two bits per ID plus its stack. Ignoring overhead, 64-bit stack IDs and maximum depth D cost `N/4+8D` bytes versus reconstruction's `N/2`. Payload crossover is D>N/32; for 32-bit IDs it is D>N/16. Test these predictions against measured allocations and RSS.

## Canonical correctness evidence

Small tests compare independent reference traversals. Real-graph callbacks produce a canonical byte stream:

- Header: 15 ASCII bytes `SEGA-EVENTS-v1\n`.
- BFS: `B` + vertex uint64 + distance uint64 (17 bytes).
- DFS discovery: `D` + vertex uint64 + parent uint64 (17 bytes).
- DFS finish: `F` + vertex uint64 (9 bytes).
- All integers are little-endian; root parent is `UINT64_MAX`.

SHA-256 covers exactly those bytes including the header. BFS emits `visited` events; DFS emits `2*visited`. Hash equality across variants/repetitions is probabilistic evidence of identical ordered output, not a proof. Optional event files are checked for size, SHA-256, and exact byte equality with bounded RAM. Agreement alone cannot exclude a shared backend error; navigation auditing and independent small references provide additional checks.

## Memory and timing interpretation

`aux_fixed_bytes` measures requested fixed-array storage. `aux_dynamic_peak_bytes` measures requested dynamic container allocations including deque blocks/maps and transient vector-growth overlap. `aux_peak_bytes` combines fixed and dynamic requested sizes. These exclude graph storage, allocator headers, local stack objects, and downstream output retention; they are not RSS.

`graph_rss_kb` observes process RSS after loading and valid-ID scanning; `graph_peak_rss_kb` records the pre-traversal lifetime peak. `peak_rss_kb` is the whole-process lifetime peak and can be dominated by loading. Subtracting high-water marks does not give exact auxiliary usage. Unavailable metrics remain unavailable.

Correctness runs enable hashing/event output. Measurement runs disable both. `wall_ms` includes traversal-state allocation and release; load and valid-ID scan times are separate. `process_wall_seconds` includes startup, loading, scanning, and traversal. Report compiler, CPU, OS, source/root policy, input identity, and run conditions with results.

The MEGAHIT adapter preflights degrees before filling neighbor buffers. This adds navigation cost compared with the historical release. `incoming_calls` counts oracle API calls; each backend call currently includes degree preflight and enumeration, so it does not count underlying MEGAHIT backward operations directly.
