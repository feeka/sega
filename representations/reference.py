"""Independent, deliberately small Python reference. Never used for timed native traversals."""
from collections import Counter, deque
import gzip
import math
from pathlib import Path
import random
import re

DNA = "ACGT"

def rc(s):
    return s.translate(str.maketrans("ACGT", "TGCA"))[::-1]

def sequences(path, k=31, chunk_size=4096):
    """FASTA chunks overlap by k-1; FASTQ accepts wrapped sequence/quality.

    Chunking bounds FASTA buffering and preserves the k-mer SET, not abundance.
    Ambiguous bases are split later and never joined across an N or record end.
    """
    opener = gzip.open if str(path).endswith(".gz") else open
    with opener(path, "rt", encoding="ascii") as stream:
        first = stream.readline()
        while first and not first.strip():
            first = stream.readline()
        if first.startswith(">"):
            pending = "";line_start=True
            while True:
                line=stream.readline(chunk_size)
                if not line:break
                if line_start and line.startswith(">"):
                    if pending: yield pending.upper()
                    pending = ""
                    while line and not line.endswith('\n'):line=stream.readline(chunk_size)
                    line_start=True
                else:
                    line_start=line.endswith('\n')
                    pending += line.strip()
                    while len(pending) >= chunk_size:
                        yield pending[:chunk_size].upper()
                        pending = pending[chunk_size-k+1:]
            if pending: yield pending.upper()
        elif first.startswith("@"):
            header = first
            while header:
                if not header.startswith("@"): raise ValueError("Malformed FASTQ header")
                parts = []
                for line in stream:
                    if line.startswith("+"): break
                    parts.append(line.strip())
                else: raise ValueError("FASTQ record has no + separator")
                seq = "".join(parts).upper()
                quality = 0
                while quality < len(seq):
                    line = stream.readline()
                    if not line: raise ValueError("Truncated FASTQ quality")
                    quality += len(line.rstrip("\r\n"))
                if quality != len(seq): raise ValueError("FASTQ sequence/quality lengths differ")
                for offset in range(0, len(seq), chunk_size-k+1):
                    yield seq[offset:offset+chunk_size]
                header = stream.readline()
        elif first:
            raise ValueError(f"Expected FASTA/FASTQ: {path}")

def prepare(paths, output, query_file, k, sample_count, seed, exhaustive=False, max_reference=100000):
    rng = random.Random(seed)
    reference = set() if exhaustive else None
    reservoir, occurrences, records, bases = [], 0, 0, 0
    weight = None
    skip = 0
    with Path(output).open("w") as out:
        for path in paths:
            for seq in sequences(path, k):
                for match in re.finditer("[ACGT]+", seq):
                    segment = match.group()
                    if len(segment) < k: continue
                    # All tools receive the SAME strand-closed, unweighted input.
                    for s in (segment, rc(segment)):
                        out.write(f">segment_{records}\n{s}\n")
                        records += 1
                        bases += len(s)
                        positions = len(s)-k+1
                        occurrences += positions
                        if reference is not None:
                            for i in range(positions):
                                word = s[i:i+k]
                                reference.add(word)
                                if len(reference) > max_reference:
                                    raise ValueError("Mini audit exceeds reference limit; use run for large data")
                        # Vitter's skip reservoir: large inputs do not execute a
                        # Python loop or RNG call for every k-mer occurrence.
                        i = 0
                        while i < positions and len(reservoir) < sample_count:
                            reservoir.append(s[i:i+k]);i += 1
                        if len(reservoir) == sample_count:
                            if weight is None:
                                weight = min(math.nextafter(1.0,0.0),math.exp(math.log(1-rng.random())/sample_count))
                                skip = int(math.log(1-rng.random())/math.log1p(-weight))
                            while i+skip < positions:
                                i += skip
                                reservoir[rng.randrange(sample_count)] = s[i:i+k]
                                i += 1
                                weight *= math.exp(math.log(1-rng.random())/sample_count)
                                skip = int(math.log(1-rng.random())/math.log1p(-weight))
                            skip -= positions-i
    if not occurrences: raise ValueError("No valid k-mers in input")
    positives = list(dict.fromkeys(reservoir))
    candidates = []
    for word in positives:
        for c in DNA:
            candidates.extend((word[1:]+c, c+word[:-1]))
    # Candidates are not described as negatives until an independent reference
    # establishes their absence. Large runs only have sampled cross-tool checks.
    queries = list(dict.fromkeys(positives + candidates +
        ["".join(rng.choices(DNA,k=k)) for _ in range(sample_count)]))
    Path(query_file).write_text("\n".join(queries)+"\n")
    return {"normalized_records":records, "normalized_bases":bases,
            "normalized_kmer_occurrences":occurrences, "query_count":len(queries),
            "sampled_positive_kmers":positives, "query_sampling":"occurrence-reservoir-then-deduplicate",
            "policy":"ACGT only; split ambiguity; K-1 chunk overlap; reverse-complement closure; abundance >=1; unweighted"}, reference

def adjacency(words):
    return {s:[s[1:]+c for c in DNA if s[1:]+c in words] for s in words}

def metrics(words):
    outgoing = adjacency(words)
    incoming = {s:[c+s[:-1] for c in DNA if c+s[:-1] in words] for s in words}
    ih, oh = Counter(map(len,incoming.values())), Counter(map(len,outgoing.values()))
    return {"oriented_kmers":len(words), "canonical_kmers":len({min(s,rc(s)) for s in words}),
            "arcs":sum(map(len,outgoing.values())),
            "indegree_histogram":[ih[i] for i in range(5)], "outdegree_histogram":[oh[i] for i in range(5)],
            "tips_including_isolates":sum(not outgoing[s] or not incoming[s] for s in words),
            "isolates":sum(not outgoing[s] and not incoming[s] for s in words),
            "branch_vertices":sum(len(outgoing[s])>1 or len(incoming[s])>1 for s in words),
            "self_loops":sum(s in outgoing[s] for s in words)}, outgoing, incoming

def distances(outgoing, source):
    distance = {source:0}; queue = deque([source])
    while queue:
        v = queue.popleft()
        for w in outgoing[v]:
            if w not in distance: distance[w]=distance[v]+1; queue.append(w)
    return distance

def walk_metrics(outgoing,queries):
    hits=steps=0
    for word in queries:
        if word not in outgoing:continue
        hits+=1
        for step in range(100):
            neighbors=sorted(outgoing[word])
            if not neighbors:break
            word=neighbors[step%len(neighbors)];steps+=1
    return {'query_count':len(queries),'hits':hits,'neighbor_or_walk_steps':steps}

def component_metrics(outgoing, incoming):
    unseen=set(outgoing); sizes=[]
    while unseen:
        stack=[unseen.pop()]; size=0
        while stack:
            v=stack.pop(); size+=1
            for w in outgoing[v]+incoming[v]:
                if w in unseen: unseen.remove(w);stack.append(w)
        sizes.append(size)
    return {"weak_components":len(sizes),"largest_weak_component_kmers":max(sizes,default=0)}

def strong_component_metrics(outgoing,incoming):
    visited=set();order=[]
    for root in outgoing:
        if root in visited:continue
        visited.add(root);stack=[(root,iter(outgoing[root]))]
        while stack:
            v,children=stack[-1]
            child=next(children,None)
            if child is None:order.append(v);stack.pop()
            elif child not in visited:visited.add(child);stack.append((child,iter(outgoing[child])))
    visited.clear();sizes=[]
    for root in reversed(order):
        if root in visited:continue
        visited.add(root);stack=[root];size=0
        while stack:
            v=stack.pop();size+=1
            for child in incoming[v]:
                if child not in visited:visited.add(child);stack.append(child)
        sizes.append(size)
    return {"strong_components":len(sizes),"largest_strong_component_kmers":max(sizes,default=0),"singleton_strong_components":sizes.count(1)}

def unitig_metrics(outgoing,incoming):
    # Independent edge partition, unlike the native vertex bitmap algorithm.
    remaining={(v,w) for v,ws in outgoing.items() for w in ws}
    paths=cycles=isolates=maximum=total=0
    for v in outgoing:
        if not incoming[v] and not outgoing[v]: isolates+=1
        if len(incoming[v])==1 and len(outgoing[v])==1: continue
        for w in outgoing[v]:
            remaining.remove((v,w)); length=1; paths+=1
            while len(incoming[w])==1 and len(outgoing[w])==1:
                nxt=outgoing[w][0];remaining.remove((w,nxt));w=nxt;length+=1
            total+=length;maximum=max(maximum,length)
    while remaining:
        v,w=remaining.pop();length=1;cycles+=1
        while w!=v:
            nxt=outgoing[w][0];remaining.remove((w,nxt));w=nxt;length+=1
        total+=length;maximum=max(maximum,length)
    return {"oriented_maximal_nonbranching_paths":paths,"isolated_directed_cycles":cycles,"isolates":isolates,
            "partitioned_arcs":total,"max_path_or_cycle_arcs":maximum}

def verify_trace(path, words):
    expected, out, inc = metrics(words)
    actual={}
    for line in Path(path).read_text().splitlines():
        s,a,b=line.split("\t")
        if s in actual: raise AssertionError(f"Duplicate graph label {s}")
        actual[s]=(a.split(",") if a else [],b.split(",") if b else [])
    if set(actual)!=words: raise AssertionError(f"K-mer set mismatch: missing {len(words-set(actual))}, extra {len(set(actual)-words)}")
    for s,(a,b) in actual.items():
        if sorted(a)!=sorted(out[s]) or sorted(b)!=sorted(inc[s]): raise AssertionError(f"Adjacency mismatch at {s}")
    return {"status":"pass","exact_oriented_kmers":len(words),"exact_arcs":expected["arcs"]}

def verify_traversal(path, operation, outgoing, source):
    lines=[line.split("\t") for line in Path(path).read_text().splitlines()]
    reachable=distances(outgoing,source) if source is not None else dict.fromkeys(outgoing,0)
    if operation.startswith("bfs"):
        actual={row[1]:int(row[2]) for row in lines}
        if source is None:
            reachable={}
            for row in lines:
                v,d=row[1],int(row[2])
                if d!=0:continue
                if v in reachable:raise AssertionError("BFS forest root already visited")
                reachable[v]=0;queue=deque([v])
                while queue:
                    w=queue.popleft()
                    for child in outgoing[w]:
                        if child not in reachable:reachable[child]=reachable[w]+1;queue.append(child)
            if set(reachable)!=set(outgoing):raise AssertionError("BFS forest omitted vertices")
        if len(actual)!=len(lines) or actual!=reachable: raise AssertionError("BFS distances/discovery uniqueness mismatch")
    else:
        stack=[]; discovered=set(); finished=set()
        for row in lines:
            if row[0]=="D":
                _,v,parent=row
                if v in discovered: raise AssertionError("DFS duplicate discovery")
                if not stack:
                    if (source is not None and v!=source) or parent!="-": raise AssertionError("DFS root mismatch")
                elif parent!=stack[-1] or v not in outgoing[parent]: raise AssertionError("DFS parent/stack mismatch")
                discovered.add(v);stack.append(v)
            else:
                _,v=row
                if not stack or stack.pop()!=v or v in finished: raise AssertionError("DFS finish nesting mismatch")
                if any(w not in discovered for w in outgoing[v]): raise AssertionError("DFS finished before exploring successor")
                finished.add(v)
        if stack or discovered!=set(reachable) or finished!=discovered: raise AssertionError("DFS reachability/finish mismatch")
    return {"status":"pass","reachable_kmers":len(reachable)}

def mini_fixture(path,k,seed=1729):
    rng=random.Random(seed)
    chain="".join(rng.choices(DNA,k=2000)); branch=list(chain);branch[900]="ACGT"[(DNA.index(branch[900])+1)%4]
    records=[chain,"".join(branch),"A"*(k+10),"ACGT"*(k+3),"G"*k,
             "".join(rng.choices(DNA,k=1000)),"ACGTNNN"+chain[:k],chain[:k],chain[:k-1],chain]
    hub=''.join(rng.choices(DNA,k=k))
    for c in DNA:records.extend((c+hub,hub+c))
    words=set()
    for record in records:
        for i in range(len(record)-k+1):
            s=record[i:i+k]
            if set(s)<=set(DNA):words.update((s,rc(s)))
    # Include a true isolated canonical k-mer, independently of random overlaps.
    while True:
        island=''.join(rng.choices(DNA,k=k));pair={island,rc(island)};universe=words|pair
        if not pair&words and all(s[1:]+c not in universe and c+s[:-1] not in universe for s in pair for c in DNA):break
    records.append(island)
    Path(path).write_text("".join(f">fixture_{i}\n{s}\n" for i,s in enumerate(records)))
