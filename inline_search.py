#!/usr/bin/env python3

import sys
import tempfile
import os

from clang_inline import Command

class Function:
    def __init__(self, name):
        self.name = name

class CallSite:
    def __init__(self, caller, callee, loc):
        self.caller = caller
        self.callee = callee
        self.loc = loc

class InlineDecision:
    class Status:
        NA = 0
        DEFAULT = 1
        ACCEPTED = 2
        REJECTED = 3

    def __init__(self, caller, callee, loc, decision, status):
        self.caller = caller
        self.callee = callee
        self.loc = loc
        self.decision = decision
        self.status = status
    
    def __str__(self):
        status = ""
        if self.status == InlineDecision.Status.DEFAULT:
            status = "DEFAULT"
        elif self.status == InlineDecision.Status.ACCEPTED:
            status = "ACCEPTED"
        elif self.status == InlineDecision.Status.REJECTED:
            status = "REJECTED"
        return self.caller + " -> " + self.callee + " @ " + self.loc + " " + self.decision + " " + status

    def from_str(decision_string):
        caller, _, callee, _, loc, _, decision, status = decision_string.split(" ")
        if status == "DEFAULT":
            status = InlineDecision.Status.DEFAULT
        elif status == "ACCEPTED":
            status = InlineDecision.Status.ACCEPTED
        elif status == "REJECTED":
            status = InlineDecision.Status.REJECTED
        else:
            print("Unknown status: " + status)
            status = InlineDecision.Status.DEFAULT
        return InlineDecision(caller, callee, loc, decision, status)

class CallGraph:
    class Node:
        def __init__(self):
            self.call_sites = []
    
    def __init__(self):
        self.nodes = {}

    def copy(self):
        new_graph = CallGraph()
        for node in self.nodes:
            new_graph.nodes[node] = CallGraph.Node()
            new_graph.nodes[node].call_sites = self.nodes[node].call_sites.copy()
        return new_graph

    def empty(self):
        for node in self.nodes:
            if len(self.nodes[node].call_sites):
                return False
        return True

    def with_call_removed(self, call_site, inlined):
        new_graph = CallGraph()
        new_graph = self.copy()
        new_graph.nodes[call_site.caller].call_sites.remove(call_site)
        if inlined:
            if call_site.callee in self.nodes:
                for moved_call_site in self.nodes[call_site.callee].call_sites:
                    if moved_call_site.callee == call_site.caller:
                        continue
                    new_graph.nodes[call_site.callee].call_sites.remove(moved_call_site)
                    new_graph.add_call_site(call_site.caller, moved_call_site.callee, moved_call_site.loc)
        return new_graph

    def add_call_site(self, caller, callee, loc):
        if caller not in self.nodes:
            self.nodes[caller] = CallGraph.Node()
        self.nodes[caller].call_sites.append(CallSite(caller, callee, loc))
    
    def __str__(self):
        output = ""
        for fn in self.nodes:
            for call_site in self.nodes[fn].call_sites:
                output += fn + " -> " + call_site.callee + " @ " + call_site.loc + "\n"
        return output[:-1]

class RunInfo:
    def __init__(self, stdout, stderr, ignore_original_call_graph=False, ignore_final_call_graph=False, ignore_dead_code=False, ignore_inline_decisions=False):
        self.stdout = stdout
        self.stderr = stderr

        if len(stderr):
            self.error = True
        else:
            self.error = False

            self.functions = set()
            self.call_graph = CallGraph()
            self.eliminated_calls = set()
            self.inline_decisions = []

            class ReadState:
                ORIGINAL_CALL_GRAPH = 0
                FINAL_CALL_GRAPH = 1
                DEAD_CODE = 2
                INLINE_DECISIONS = 3
                NONE = 4
            state = ReadState.NONE
            for line in stdout.splitlines():
                if line.startswith("Original Call Graph:"):
                    state = ReadState.ORIGINAL_CALL_GRAPH
                elif line.startswith("Final Call Graph:"):
                    state = ReadState.FINAL_CALL_GRAPH
                elif line.startswith("Eliminated Calls:"):
                    state = ReadState.DEAD_CODE
                elif line.startswith("Decisions:"):
                    state = ReadState.INLINE_DECISIONS
                elif state != ReadState.NONE:
                    if state == ReadState.ORIGINAL_CALL_GRAPH:
                        if ignore_original_call_graph:
                            continue
                        caller, _, callee, _, loc = line.split(" ")
                        self.call_graph.add_call_site(caller, callee, loc)
                        self.functions.add(caller)
                        self.functions.add(callee)
                    elif state == ReadState.FINAL_CALL_GRAPH:
                        if ignore_final_call_graph:
                            continue
                        # for now we don't care about the final call graph
                    elif state == ReadState.DEAD_CODE:
                        if ignore_dead_code:
                            continue
                        self.eliminated_calls.add(line)
                    elif state == ReadState.INLINE_DECISIONS:
                        if ignore_inline_decisions:
                            continue
                        self.inline_decisions.append(InlineDecision.from_str(line))

already_tried = set()
smallest_output = None
def recursive_call_graph_search(graph, decisions):
    decision_str = "\n".join([str(decision) for decision in decisions])
    if str(decision_str) in already_tried:
        print("Already tried this decision")
        print("This shouldn't happen")
        for decision in decisions:
            print(decision)
        input("Press enter to continue...")
        return
    already_tried.add(decision_str)
    if graph.empty():
        # create a temporary file with the decisions
        advice_file = tempfile.NamedTemporaryFile(mode="w")
        lines = ["Decisions:"] + [str(decision) for decision in decisions]
        advice_file.write("\n".join(lines))
        advice_file.flush()

        # run the compiler with the advice file
        output_file = tempfile.NamedTemporaryFile(mode="w")
        run_info = RunInfo(*command.call(output_file.name, advice_file.name), ignore_original_call_graph=True, ignore_final_call_graph=True, ignore_dead_code=True, ignore_inline_decisions=False)
        if run_info.error:
            print("Error:")
            print(run_info.stderr)
            print("Trying graph:")
            print(graph)
            print("Decisions:")
            for decision in decisions:
                print(decision)
            print("Resulted in decision taken:")
            for decision in run_info.inline_decisions:
                print(decision)
            input("Press enter to continue...")
            return

        # check file size of output_file
        size = os.path.getsize(output_file.name)

        # print the decisions
        print("\nSize: " + str(size) + " bytes")
        print("With decisions:")
        for decision in decisions:
            print(decision)
        
        global smallest_output
        if (smallest_output is None) or (size < smallest_output[0]):
            smallest_output = (size, decisions)
        # input("Press enter to continue...")
    else:
        for node in graph.nodes.values():
            for call_site in node.call_sites:
                new_graph = graph.with_call_removed(call_site, True)
                new_decisions = decisions.copy()
                new_decisions.append(InlineDecision(call_site.caller, call_site.callee, call_site.loc, "inline", InlineDecision.Status.NA))
                recursive_call_graph_search(new_graph, new_decisions)

                # try not inlining
                new_graph = graph.with_call_removed(call_site, False)
                new_decisions = decisions.copy()
                new_decisions.append(InlineDecision(call_site.caller, call_site.callee, call_site.loc, "no-inline", InlineDecision.Status.NA))
                recursive_call_graph_search(new_graph, new_decisions)

if(__name__ == "__main__"):
    command = Command()

    source_files = ["test/test.c"]
    output_file = "test/out/test.s"
    cli_args = ["-S"]
    command.extra_args = source_files + cli_args

    tmp_file = tempfile.NamedTemporaryFile(mode="w")
    default_info = RunInfo(*command.call(tmp_file.name, output_file))

    if default_info.error:
        print("Output: \n" + default_info.stdout)
        print("Error: \n" + default_info.stderr)
        sys.exit(1)

    # print starting call graph
    print("Starting call graph:")
    print(default_info.stdout)

    # Now we start our exhaustive search
    print("Starting exhaustive search...")
    recursive_call_graph_search(default_info.call_graph, [])

    print("For call graph:")
    print(default_info.call_graph)
    print("Smallest output:")
    print(smallest_output[0])
    print("With decisions:")
    for decision in smallest_output[1]:
        print(decision)
        
