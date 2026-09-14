# Milestone A: reliable UIA base

This branch establishes the reliability boundary for UI Automation work.

`uia::SingleSlotQueue` is a thread-safe latest-wins queue. It permits at most
one pending request, so pointer movement cannot create an unbounded backlog
while a provider is slow or hung. Stale requests are replaced before execution.

The production UIA worker should build on this boundary with:

- One dedicated worker thread and one COM apartment.
- A request generation/token so late provider results cannot overwrite a newer
  target.
- A bounded provider-call timeout and cancellation/abandonment path.
- Explicit handling for provider exceptions and invalid UIA elements.
- Main-thread publication of only the current generation.
- Tests using a fake provider for success, timeout, exception, stale-result,
  and rapid-pointer-movement cases.

This first slice intentionally keeps provider invocation out of the queue class;
COM initialization, timeout mechanics, and identity publication belong in the
next focused commit so each behavior can be tested independently.
