// SPDX-License-Identifier: GPL-2.0-or-later
#include "modules/tbc/director.h"

#include <algorithm>

namespace pstf::tbc {

namespace {
InitKey Key(const Combatant& c) {
  return {c.initiative, c.r.side == Side::Party, c.roll, c.r.id};
}
}  // namespace

Combatant* Director::Get(int32_t id) {
  for (auto& c : all_)
    if (c.r.id == id && c.active) return &c;
  return nullptr;
}
const Combatant* Director::Find(int32_t id) const {
  for (auto& c : all_)
    if (c.r.id == id) return &c;
  return nullptr;
}

std::vector<Combatant*> Director::Sorted() {
  std::vector<Combatant*> v;
  for (auto& c : all_)
    if (c.active) v.push_back(&c);
  std::sort(v.begin(), v.end(),
            [](const Combatant* a, const Combatant* b) { return InitBefore(Key(*a), Key(*b)); });
  return v;
}

std::vector<int32_t> Director::Order() const {
  auto self = const_cast<Director*>(this);
  std::vector<int32_t> ids;
  for (auto* c : self->Sorted()) ids.push_back(c->r.id);
  return ids;
}

std::vector<Event> Director::TakeEvents() {
  std::vector<Event> out;
  out.swap(events_);
  return out;
}

void Director::Start(const std::vector<Recruit>& who, uint32_t world_tick) {
  all_.clear();
  events_.clear();
  start_tick_ = world_tick;
  round_ = 0;
  rounds_done_ = 0;
  busy_ = Busy::None;
  current_ = -1;
  running_ = true;
  for (const auto& r : who) {
    Combatant c;
    c.r = r;
    c.roll = rng_.Die(10);
    c.initiative = Initiative(c.roll, r.dex_reaction_bonus);
    all_.push_back(c);
  }
  events_.push_back({EvType::CombatStart, -1, static_cast<int32_t>(all_.size())});
  if (CheckEnd()) return;
  StartRound();
}

void Director::StartRound() {
  ++round_;
  for (auto& c : all_) c.done_this_round = c.started = false;
  events_.push_back({EvType::RoundStart, -1, static_cast<int32_t>(round_)});
  Advance();
}

bool Director::InCurrentGroup(const Combatant& c) const {
  // The group = the maximal run of party members around the current slot in initiative order.
  const Combatant* cur = Find(current_);
  if (!cur || cur->r.side != Side::Party || c.r.side != Side::Party) return false;
  auto order = Order();
  auto pos = [&](int32_t id) {
    return static_cast<int>(std::find(order.begin(), order.end(), id) - order.begin());
  };
  int a = pos(current_), b = pos(c.r.id);
  if (a > b) std::swap(a, b);
  for (int i = a; i <= b; ++i) {
    const Combatant* x = Find(order[i]);
    if (!x || x->r.side != Side::Party) return false;
  }
  return true;
}

void Director::Advance() {
  if (!running_) return;
  for (auto* c : Sorted()) {
    if (c->done_this_round || c->first_round > round_) continue;
    BeginTurn(*c);
    return;
  }
  ++rounds_done_;
  StartRound();
}

void Director::BeginTurn(Combatant& c) {
  current_ = c.r.id;
  busy_ = Busy::None;
  c.started = true;
  c.move_left = c.r.move_budget;
  c.actions_left = 1;
  c.attacks_left = 0;
  c.reaction_used = false;
  events_.push_back({EvType::TurnStart, c.r.id, static_cast<int32_t>(round_)});
}

void Director::FinishTurn(Combatant& c) {
  c.done_this_round = true;
  ++c.own_rounds;
  c.move_left = c.actions_left = c.attacks_left = 0;
  events_.push_back({EvType::TurnEnd, c.r.id, static_cast<int32_t>(round_)});
  busy_ = Busy::None;
  // Another party member of the same group still to act keeps the group's turn.
  for (auto& o : all_) {
    if (o.active && !o.done_this_round && o.r.id != c.r.id && o.first_round <= round_ &&
        InCurrentGroup(o)) {
      Resume(o);
      return;
    }
  }
  current_ = -1;
  if (!CheckEnd()) Advance();
}

void Director::MaybeAutoEnd(Combatant& c) {
  if (busy_ == Busy::None && c.actions_left == 0 && c.attacks_left == 0 && c.move_left <= 0)
    FinishTurn(c);
}

bool Director::SwitchTo(int32_t id) {
  Combatant* cur = Get(current_);
  Combatant* c = Get(id);
  if (!running_ || !cur || !c || busy_ != Busy::None || c->done_this_round ||
      c->first_round > round_ || !InCurrentGroup(*c))
    return false;
  if (id != current_) Resume(*c);
  return true;
}

void Director::Resume(Combatant& c) {
  if (c.started) {
    current_ = c.r.id;  // keeps what it has not spent
  } else {
    BeginTurn(c);
  }
}

bool Director::BeginMove(int32_t id, int pixels) {
  Combatant* c = Get(id);
  if (!c || id != current_ || busy_ != Busy::None || pixels <= 0 || pixels > c->move_left)
    return false;
  busy_ = Busy::Moving;
  return true;
}

void Director::MoveFinished(int32_t id, int spent) {
  Combatant* c = Get(id);
  if (!c || id != current_ || busy_ != Busy::Moving) return;
  c->move_left = std::max(0, c->move_left - std::max(0, spent));
  busy_ = Busy::None;
  MaybeAutoEnd(*c);
}

bool Director::BeginSwing(int32_t id) {
  Combatant* c = Get(id);
  if (!c || id != current_ || busy_ != Busy::None) return false;
  if (c->attacks_left == 0) {
    if (c->actions_left == 0) return false;
    const int n = AttacksThisRound(c->r.apr_halves, c->own_rounds);
    if (n == 0) return false;
    c->actions_left = 0;
    c->attacks_left = n;
  }
  --c->attacks_left;
  busy_ = Busy::Attacking;
  events_.push_back({EvType::Swing, id, c->attacks_left});
  return true;
}

void Director::SwingResolved(int32_t id) {
  Combatant* c = Get(id);
  if (!c || id != current_ || busy_ != Busy::Attacking) return;
  busy_ = Busy::None;
  if (!CheckEnd()) MaybeAutoEnd(*c);
}

bool Director::BeginAction(int32_t id) {
  Combatant* c = Get(id);
  if (!c || id != current_ || busy_ != Busy::None || c->actions_left == 0) return false;
  c->actions_left = 0;
  busy_ = Busy::Acting;
  return true;
}

void Director::ActionResolved(int32_t id) {
  Combatant* c = Get(id);
  if (!c || id != current_ || busy_ != Busy::Acting) return;
  busy_ = Busy::None;
  if (!CheckEnd()) MaybeAutoEnd(*c);
}

bool Director::EndTurn(int32_t id) {
  Combatant* c = Get(id);
  if (!c || id != current_ || busy_ != Busy::None) return false;  // never end mid-walk/mid-swing
  FinishTurn(*c);
  return true;
}

bool Director::Reaction(int32_t id) {
  Combatant* c = Get(id);
  if (!c || id == current_ || c->reaction_used) return false;
  c->reaction_used = true;
  events_.push_back({EvType::Reaction, id, current_});
  return true;
}

void Director::Join(const Recruit& r) {
  if (!running_ || Find(r.id)) return;
  Combatant c;
  c.r = r;
  c.roll = rng_.Die(10);
  c.initiative = Initiative(c.roll, r.dex_reaction_bonus);
  c.first_round = round_;
  // A newcomer whose slot has already passed this round waits for the next round.
  if (const Combatant* cur = Find(current_)) {
    if (InitBefore(Key(c), Key(*cur))) c.first_round = round_ + 1;
  }
  all_.push_back(c);
  events_.push_back({EvType::Joined, r.id, static_cast<int32_t>(all_.back().first_round)});
}

void Director::Remove(int32_t id) {
  Combatant* c = Get(id);
  if (!c) return;
  const bool was_current = id == current_;
  c->active = false;
  events_.push_back({EvType::Left, id, static_cast<int32_t>(round_)});
  if (CheckEnd()) return;
  if (was_current) {
    busy_ = Busy::None;
    current_ = -1;
    Advance();
  }
}

bool Director::SetSide(int32_t id, Side side) {
  Combatant* c = Get(id);
  if (!running_ || !c || c->r.side == side) return false;
  c->r.side = side;
  CheckEnd();
  return true;
}

bool Director::CheckEnd() {
  bool party = false, hostile = false;
  for (auto& c : all_) {
    if (!c.active) continue;
    (c.r.side == Side::Party ? party : hostile) = true;
  }
  if (party && hostile) return false;
  running_ = false;
  current_ = -1;
  busy_ = Busy::None;
  events_.push_back({EvType::CombatEnd, -1, party ? 0 : 1});
  return true;
}

}  // namespace pstf::tbc
