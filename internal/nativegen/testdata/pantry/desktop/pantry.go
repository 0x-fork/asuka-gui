// The pantry's Go, for the desktop hosts.
package pantry

import (
	"context"
	"strings"
)

type goPantry struct{ items []Item }

func newPantry() (Pantry, error) { return &goPantry{}, nil }
func newShelf() (Shelf, error)   { return &goShelf{}, nil }

func (p *goPantry) Name() (string, error) { return "Go's pantry", nil }
func (p *goPantry) Items(filter string) ([]Item, error) {
	var out []Item
	for _, it := range p.items {
		if strings.Contains(it.Name, filter) {
			out = append(out, it)
		}
	}
	return out, nil
}
func (p *goPantry) Add(item Item) error {
	if len(p.items) > 99 {
		return ErrBusy
	}
	p.items = append(p.items, item)
	return nil
}
func (p *goPantry) Cook(ctx context.Context, recipe string, progress func(float64)) (Item, error) {
	progress(1)
	return Item{Name: recipe}, ctx.Err()
}
func (p *goPantry) Ask(question func(string) bool) (bool, error) { return question("ready?"), nil }
func (p *goPantry) Open(name string) (Shelf, error)               { return &goShelf{}, nil }
func (p *goPantry) Show() error                                    { return nil }
func (p *goPantry) Counts() (map[string]int, error)                { return map[string]int{"items": len(p.items)}, nil }

type goShelf struct{}

func (goShelf) Stack(count int, level Level) error { return nil }
func (goShelf) Close() error                       { return nil }
