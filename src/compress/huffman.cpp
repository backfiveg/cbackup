#include "compress/huffman.h"

#include <array>
#include <cstring>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

namespace cbackup {
namespace compress {
namespace huffman {

namespace {

// Binary tree node. Owned via unique_ptr for leak-free RAII.
struct Node {
    uint32_t freq = 0;
    int      sym  = -1;   // -1 for internal nodes
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
};

// Priority queue ordering: smaller frequency first (min-heap).
struct NodeCmp {
    bool operator()(const Node* a, const Node* b) const {
        if (a->freq != b->freq) return a->freq > b->freq;
        return a->sym > b->sym;  // deterministic tie-break
    }
};

// Recursively build the canonical code table.
void build_codes(const Node* node,
                 std::string& prefix,
                 std::array<std::string, 256>& codes) {
    if (!node) return;
    if (node->sym >= 0) {
        // Leaf. If the tree has a single symbol the prefix is empty; assign "0".
        codes[static_cast<size_t>(node->sym)] = prefix.empty() ? "0" : prefix;
        return;
    }
    prefix.push_back('0');
    build_codes(node->left.get(), prefix, codes);
    prefix.pop_back();
    prefix.push_back('1');
    build_codes(node->right.get(), prefix, codes);
    prefix.pop_back();
}

// Append a little-endian integer to a byte buffer.
template <typename T>
void put_int(std::vector<uint8_t>& buf, T value) {
    for (size_t i = 0; i < sizeof(T); ++i)
        buf.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
}

template <typename T>
T get_int(const uint8_t* p) {
    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        value |= static_cast<T>(p[i]) << (8 * i);
    return value;
}

// Build the Huffman tree from a frequency table. Returns the root (owning).
std::unique_ptr<Node> build_tree(const std::array<uint32_t, 256>& freq) {
    // Storage keeps ownership; the priority queue holds raw pointers.
    std::vector<std::unique_ptr<Node>> pool;
    std::priority_queue<Node*, std::vector<Node*>, NodeCmp> pq;

    for (int s = 0; s < 256; ++s) {
        if (freq[static_cast<size_t>(s)] == 0) continue;
        auto n = std::make_unique<Node>();
        n->freq = freq[static_cast<size_t>(s)];
        n->sym  = s;
        pq.push(n.get());
        pool.push_back(std::move(n));
    }

    if (pq.empty()) return nullptr;

    while (pq.size() > 1) {
        Node* a = pq.top(); pq.pop();
        Node* b = pq.top(); pq.pop();
        auto parent = std::make_unique<Node>();
        parent->freq  = a->freq + b->freq;
        parent->sym   = -1;
        // Reparent by finding the owning unique_ptrs in the pool.
        for (auto& up : pool) {
            if (up.get() == a) parent->left  = std::move(up);
            else if (up.get() == b) parent->right = std::move(up);
        }
        pq.push(parent.get());
        pool.push_back(std::move(parent));
    }

    Node* root = pq.top();
    // Transfer ownership of the root out of the pool.
    std::unique_ptr<Node> owned_root;
    for (auto& up : pool) {
        if (up.get() == root) { owned_root = std::move(up); break; }
    }
    return owned_root;
}

}  // namespace

std::vector<uint8_t> encode(const uint8_t* data, size_t len) {
    if (len == 0 || data == nullptr) return {};

    // 1. Frequency table.
    std::array<uint32_t, 256> freq{};
    for (size_t i = 0; i < len; ++i) freq[data[i]]++;

    // 2. Build tree + code table.
    std::unique_ptr<Node> root = build_tree(freq);
    std::array<std::string, 256> codes;
    std::string prefix;
    build_codes(root.get(), prefix, codes);

    // 3. Serialize header (frequency table).
    std::vector<uint8_t> out;
    uint32_t sym_count = 0;
    for (int s = 0; s < 256; ++s)
        if (freq[static_cast<size_t>(s)]) sym_count++;

    put_int<uint32_t>(out, sym_count);
    for (int s = 0; s < 256; ++s) {
        if (!freq[static_cast<size_t>(s)]) continue;
        out.push_back(static_cast<uint8_t>(s));
        put_int<uint32_t>(out, freq[static_cast<size_t>(s)]);
    }

    // 4. Bit-pack the encoded stream (MSB first).
    std::vector<uint8_t> bits;
    uint8_t cur = 0;
    int nbits = 0;
    uint64_t total_bits = 0;
    for (size_t i = 0; i < len; ++i) {
        const std::string& code = codes[data[i]];
        for (char c : code) {
            cur = static_cast<uint8_t>((cur << 1) | (c == '1' ? 1 : 0));
            if (++nbits == 8) {
                bits.push_back(cur);
                cur = 0;
                nbits = 0;
            }
            ++total_bits;
        }
    }
    if (nbits > 0) {
        cur = static_cast<uint8_t>(cur << (8 - nbits));  // left-align remaining
        bits.push_back(cur);
    }

    put_int<uint64_t>(out, total_bits);
    out.insert(out.end(), bits.begin(), bits.end());
    return out;
}

std::vector<uint8_t> decode(const uint8_t* data, size_t len, size_t orig_len) {
    if (orig_len == 0) return {};
    if (data == nullptr || len < sizeof(uint32_t))
        throw std::runtime_error("huffman: truncated header");

    size_t pos = 0;
    uint32_t sym_count = get_int<uint32_t>(data + pos);
    pos += sizeof(uint32_t);
    if (sym_count == 0 || sym_count > 256)
        throw std::runtime_error("huffman: invalid symbol count");

    std::array<uint32_t, 256> freq{};
    for (uint32_t i = 0; i < sym_count; ++i) {
        if (pos + 1 + sizeof(uint32_t) > len)
            throw std::runtime_error("huffman: truncated freq table");
        uint8_t sym = data[pos++];
        uint32_t f  = get_int<uint32_t>(data + pos);
        pos += sizeof(uint32_t);
        freq[sym] = f;
    }

    if (pos + sizeof(uint64_t) > len)
        throw std::runtime_error("huffman: truncated bit count");
    uint64_t total_bits = get_int<uint64_t>(data + pos);
    pos += sizeof(uint64_t);

    std::unique_ptr<Node> root = build_tree(freq);
    if (!root) throw std::runtime_error("huffman: empty tree");

    std::vector<uint8_t> out;
    out.reserve(orig_len);

    // Special case: single distinct symbol -> tree is a lone leaf.
    if (root->sym >= 0) {
        out.assign(orig_len, static_cast<uint8_t>(root->sym));
        return out;
    }

    const Node* node = root.get();
    uint64_t consumed = 0;
    for (size_t byte_idx = pos; byte_idx < len && consumed < total_bits; ++byte_idx) {
        uint8_t byte = data[byte_idx];
        for (int b = 7; b >= 0 && consumed < total_bits; --b, ++consumed) {
            int bit = (byte >> b) & 1;
            node = bit ? node->right.get() : node->left.get();
            if (!node) throw std::runtime_error("huffman: corrupt stream");
            if (node->sym >= 0) {
                out.push_back(static_cast<uint8_t>(node->sym));
                node = root.get();
                if (out.size() == orig_len) return out;
            }
        }
    }

    if (out.size() != orig_len)
        throw std::runtime_error("huffman: decoded size mismatch");
    return out;
}

}  // namespace huffman
}  // namespace compress
}  // namespace cbackup
