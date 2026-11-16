# Moving the cache

Here's a plan for moving the **cache** to its own process, *without* downtime. It keeps `get` and `put` as they are; see [the design](https://example.com/design) for why.

1. Start the new process beside the old one.
2. Copy what's hot:
   - the sessions
   - the rate limits
3. Switch reads over, then writes.

```go
func (c *Cache) Get(key string) ([]byte, bool) {
	return c.remote.Get(key)
}
```

| Step | Risk | Rollback |
|---|---|---|
| Copy | low | drop the copy |
| Switch | medium | switch back |

> Keep the old process for a day: a rollback is then a flag.

- [x] Measure the hit rate
- [ ] Load test the new process

---

That's it: ~~three~~ four steps, and nothing users see.
