/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees. The reader of a built tree, free of libdragon so the host unit tests run it.
#include "dialog/dialogData.h"

namespace
{
  uint32_t rd32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }
  uint16_t rd16(const uint8_t* p) { return uint16_t((uint32_t(p[0]) << 8) | uint32_t(p[1])); }
  int16_t rds16(const uint8_t* p) { return static_cast<int16_t>(rd16(p)); }

  constexpr uint32_t HEADER = 32;
  constexpr uint32_t START_SIZE = 8;
}

bool P64::Dialog::TreeView::init(const void* fileData, uint32_t fileSize)
{
  *this = {};
  const auto* b = static_cast<const uint8_t*>(fileData);
  if(!b || fileSize < HEADER) return false;
  if(b[0] != 'P' || b[1] != '6' || b[2] != '4' || b[3] != 'D') return false;
  if(rd16(b + 4) != VERSION) return false;
  const uint32_t nodes = rd32(b + 16), exprs = rd32(b + 20), conds = rd32(b + 24), inputs = rd32(b + 28);
  if(nodes > fileSize || exprs > fileSize || conds > fileSize || inputs > fileSize) return false;
  if(HEADER + uint32_t(rd16(b + 8)) * START_SIZE > fileSize) return false;
  if(nodes + uint32_t(rd16(b + 6)) * 4 > fileSize) return false;
  data = b;
  size = fileSize;
  return true;
}

uint16_t P64::Dialog::TreeView::nodeCount() const { return rd16(data + 6); }
uint16_t P64::Dialog::TreeView::startCount() const { return rd16(data + 8); }
uint16_t P64::Dialog::TreeView::exprCount() const { return rd16(data + 10); }
uint16_t P64::Dialog::TreeView::conditionCount() const { return rd16(data + 12); }
uint16_t P64::Dialog::TreeView::inputCount() const { return rd16(data + 14); }

uint16_t P64::Dialog::TreeView::start(uint32_t nameHash) const
{
  const uint16_t count = startCount();
  for(uint16_t i = 0; i < count; ++i) {
    const uint8_t* s = data + HEADER + uint32_t(i) * START_SIZE;
    if(rd32(s) == nameHash) return rd16(s + 4);
  }
  return NONE;
}

const uint8_t* P64::Dialog::TreeView::nodeAt(uint16_t node) const
{
  if(node >= nodeCount()) return nullptr;
  const uint32_t off = rd32(data + rd32(data + 16) + uint32_t(node) * 4);
  return off + 4 <= size ? data + off : nullptr;
}

P64::Dialog::NodeType P64::Dialog::TreeView::type(uint16_t node) const
{
  const uint8_t* n = nodeAt(node);
  return n ? static_cast<NodeType>(n[0]) : NodeType::End;
}

uint8_t P64::Dialog::TreeView::flags(uint16_t node) const { const uint8_t* n = nodeAt(node); return n ? n[1] : 0; }
uint16_t P64::Dialog::TreeView::next(uint16_t node) const { const uint8_t* n = nodeAt(node); return n ? rd16(n + 2) : NONE; }

uint32_t P64::Dialog::TreeView::lineKey(uint16_t node) const { return rd32(nodeAt(node) + 4); }
uint8_t P64::Dialog::TreeView::lineSpeaker(uint16_t node) const { return nodeAt(node)[8]; }
P64::Dialog::Lang P64::Dialog::TreeView::lineLang(uint16_t node) const { return static_cast<Lang>(nodeAt(node)[9]); }
uint8_t P64::Dialog::TreeView::linePortrait(uint16_t node) const { return nodeAt(node)[10]; }

uint8_t P64::Dialog::TreeView::optionCount(uint16_t node) const { return nodeAt(node)[4]; }

namespace
{
  const uint8_t* optionAt(const P64::Dialog::TreeView &t, uint16_t node, uint8_t opt)
  {
    return t.nodeAt(node) + 8 + uint32_t(opt) * P64::Dialog::OPTION_SIZE;
  }
}

uint32_t P64::Dialog::TreeView::optionKey(uint16_t node, uint8_t opt) const { return rd32(optionAt(*this, node, opt)); }
uint16_t P64::Dialog::TreeView::optionNext(uint16_t node, uint8_t opt) const { return rd16(optionAt(*this, node, opt) + 4); }
uint8_t P64::Dialog::TreeView::optionFlags(uint16_t node, uint8_t opt) const { return optionAt(*this, node, opt)[6]; }
int16_t P64::Dialog::TreeView::optionShow(uint16_t node, uint8_t opt) const { return rds16(optionAt(*this, node, opt) + 8); }
int16_t P64::Dialog::TreeView::optionEnable(uint16_t node, uint8_t opt) const { return rds16(optionAt(*this, node, opt) + 10); }
uint32_t P64::Dialog::TreeView::optionEvent(uint16_t node, uint8_t opt) const { return rd32(optionAt(*this, node, opt) + 12); }
uint8_t P64::Dialog::TreeView::optionEventScope(uint16_t node, uint8_t opt) const { return optionAt(*this, node, opt)[16]; }
uint8_t P64::Dialog::TreeView::optionEventUp(uint16_t node, uint8_t opt) const { return optionAt(*this, node, opt)[17]; }
uint32_t P64::Dialog::TreeView::optionEventValue(uint16_t node, uint8_t opt) const { return rd32(optionAt(*this, node, opt) + 20); }

int16_t P64::Dialog::TreeView::branchExpr(uint16_t node) const { return rds16(nodeAt(node) + 4); }
uint16_t P64::Dialog::TreeView::branchFalse(uint16_t node) const { return rd16(nodeAt(node) + 6); }

uint32_t P64::Dialog::TreeView::eventHash(uint16_t node) const { return rd32(nodeAt(node) + 4); }
uint8_t P64::Dialog::TreeView::eventScope(uint16_t node) const { return nodeAt(node)[8]; }
uint8_t P64::Dialog::TreeView::eventUp(uint16_t node) const { return nodeAt(node)[9]; }
uint32_t P64::Dialog::TreeView::eventValue(uint16_t node) const { return rd32(nodeAt(node) + 12); }

uint16_t P64::Dialog::TreeView::gotoTree(uint16_t node) const { return rd16(nodeAt(node) + 4); }
uint32_t P64::Dialog::TreeView::gotoStart(uint16_t node) const { return rd32(nodeAt(node) + 8); }

uint32_t P64::Dialog::TreeView::endValue(uint16_t node) const { return rd32(nodeAt(node) + 4); }

const uint8_t* P64::Dialog::TreeView::expr(int16_t index) const
{
  if(index < 0 || index >= int16_t(exprCount())) return nullptr;
  const uint32_t off = rd32(data + rd32(data + 20) + uint32_t(index) * 4);
  return off < size ? data + off : nullptr;
}

uint16_t P64::Dialog::TreeView::conditionScript(uint16_t cond) const { return rd16(data + rd32(data + 24) + uint32_t(cond) * 8); }
uint16_t P64::Dialog::TreeView::conditionDataSize(uint16_t cond) const { return rd16(data + rd32(data + 24) + uint32_t(cond) * 8 + 2); }

const void* P64::Dialog::TreeView::conditionData(uint16_t cond) const
{
  return data + rd32(data + rd32(data + 24) + uint32_t(cond) * 8 + 4);
}

uint32_t P64::Dialog::TreeView::inputHash(uint8_t input) const { return rd32(data + rd32(data + 28) + uint32_t(input) * 4); }

bool P64::Dialog::runExpr(const uint8_t* code, bool (*cond)(uint16_t, void*), bool (*input)(uint8_t, void*), void* user)
{
  if(!code) return true;
  bool stack[EXPR_STACK]{};
  int sp = 0;
  for(int guard = 0; guard < 1024; ++guard) {
    const auto op = static_cast<Expr>(*code++);
    switch(op) {
      case Expr::End:
        return sp == 1 ? stack[0] : false;
      case Expr::True:
        if(sp >= EXPR_STACK) return false;
        stack[sp++] = true;
        break;
      case Expr::Cond: {
        const uint16_t idx = rd16(code);
        code += 2;
        if(sp >= EXPR_STACK) return false;
        stack[sp++] = cond ? cond(idx, user) : false;
        break;
      }
      case Expr::Input: {
        const uint8_t idx = *code++;
        if(sp >= EXPR_STACK) return false;
        stack[sp++] = input ? input(idx, user) : false;
        break;
      }
      case Expr::And: case Expr::Or: case Expr::Xor: case Expr::Not: {
        const int n = *code++;
        if(n <= 0 || n > sp) return false;
        int truthy = 0;
        for(int i = 0; i < n; ++i) truthy += stack[sp - 1 - i] ? 1 : 0;
        sp -= n;
        bool res = false;
        if(op == Expr::And) res = truthy == n;
        else if(op == Expr::Or) res = truthy > 0;
        else if(op == Expr::Xor) res = (truthy & 1) != 0;
        else res = truthy == 0;
        stack[sp++] = res;
        break;
      }
      default:
        return false;
    }
  }
  return false;
}
