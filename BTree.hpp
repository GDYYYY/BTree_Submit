#include "utility.hpp"
#include <functional>
#include <cstddef>
#include "exception.hpp"
#include <cstdio>

namespace sjtu {

    constexpr char ADDRESS[128] = "bt_in_my_heart";

    template <class Key, class Value, class Compare = std::less<Key> >
    class BTree {
    private:
        // Your private members go here
        class Block_Head {
        public:
            bool isLeaf = false;
            off_t _size = 0;
            off_t _pos = 0;
            off_t _parent = 0;
            off_t _last = 0;
            off_t _next = 0;
        };

        struct Node {
            off_t _son = 0;
            Key _key;
        };

        constexpr static off_t BLOCKSIZE = 4096;
        constexpr static off_t BGSIZE = sizeof(Block_Head);
        constexpr static off_t Keysize = sizeof(Key);
        constexpr static off_t Valsize = sizeof(Value);
        constexpr static off_t M = (BLOCKSIZE - BGSIZE) / sizeof(Node) - 1;
        constexpr static off_t L = (BLOCKSIZE - BGSIZE) / (Keysize + Valsize) - 1;
        
        class Filehead {
        public:
            off_t block_cnt = 1;
            off_t root = 0;
            off_t head = 0;
            off_t rear = 0;
           off_t _size = 0;
        };
        class DATA {
        public:
            Node val[M];
        };

        class Leaf_Data {
        public:
            pair<Key, Value> val[L];
        };

        Filehead bpt;

        static FILE* file;

        template <class T>
        static void Read(T buff, off_t buff_size, off_t pos) {
            fseek(file, long(buff_size * pos), SEEK_SET);
            fread(buff, buff_size, 1, file);
        }

        template <class T>
        static void Write(T buff, off_t buff_size, off_t pos) {
            fseek(file, long(buff_size * pos), SEEK_SET);
            fwrite(buff, buff_size, 1, file);
            fflush(file);
        }

        template <class DATA_TYPE>
        static void write_block(Block_Head* _info, DATA_TYPE* _data, off_t _pos) {
            char buff[BLOCKSIZE] = { 0 };
            memcpy(buff, _info, sizeof(Block_Head));
            memcpy(buff + BGSIZE, _data, sizeof(DATA_TYPE));
            Write(buff, BLOCKSIZE, _pos);
        }

        template <class DATA_TYPE>
        static void read_block(Block_Head* _info, DATA_TYPE* _data, off_t _pos) {
            char buff[BLOCKSIZE] = { 0 };
            Read(buff, BLOCKSIZE, _pos);
            memcpy(_info, buff, sizeof(Block_Head));
            memcpy(_data, buff + BGSIZE, sizeof(DATA_TYPE));
        }

        void update_bpt() {
            char buff[BLOCKSIZE] = { 0 };
            fseek(file, 0, SEEK_SET);
            memcpy(buff, &bpt, sizeof(bpt));
            Write(buff, BLOCKSIZE, 0);
        }

        off_t newmemory() {
            ++bpt.block_cnt;
            update_bpt();
            char buff[BLOCKSIZE] = { 0 };
            Write(buff, BLOCKSIZE, bpt.block_cnt - 1);
            return bpt.block_cnt - 1;
        }

        off_t newnode(off_t _parent) {
            auto node_pos = newmemory();
            Block_Head temp;
            DATA data;
            temp.isLeaf = false;
            temp._parent = _parent;
            temp._pos = node_pos;
            temp._size = 0;
            write_block(&temp, &data, node_pos);
            return node_pos;
        }
        
        off_t newleafnode(off_t _parent, off_t _last, off_t _next) {
            auto node_pos = newmemory();
            Block_Head temp;
            Leaf_Data leaf_data;
            temp.isLeaf = true;
            temp._parent = _parent;
            temp._pos = node_pos;
            temp._last = _last;
            temp._next = _next;
            temp._size = 0;
            write_block(&temp, &leaf_data, node_pos);
            return node_pos;
        }

        void insert_new_index(Block_Head& parent_info, DATA& parent_data, off_t origin, off_t new_pos, const Key& new_index) {

            ++parent_info._size;
            off_t p;
            for (p = parent_info._size - 2; parent_data.val[p]._son != origin; --p) {
                parent_data.val[p + 1] = parent_data.val[p];
            }
            
            parent_data.val[p + 1]._key = parent_data.val[p]._key;
            parent_data.val[p]._key = new_index;
            parent_data.val[p + 1]._son = new_pos;
        }

        Key split_leaf_node(off_t pos, Block_Head& origin_info, Leaf_Data& origin_data) {
            off_t parent_pos;
            Block_Head parent_info;
            DATA parent_data;

            if (pos == bpt.root) {
                off_t root = newnode(0);
                bpt.root = root;
                update_bpt();
                read_block(&parent_info, &parent_data, root);

                origin_info._parent = root;
                ++parent_info._size;
                parent_data.val[0]._son = pos;
                parent_pos = root;
            }

            else {
                read_block(&parent_info, &parent_data, origin_info._parent);
                parent_pos = parent_info._pos;
            }

            if (check_parent(origin_info)) {
                parent_pos = origin_info._parent;
                read_block(&parent_info, &parent_data, parent_pos);
            }

            off_t new_pos = newleafnode(parent_pos,pos,origin_info._next);

            off_t temp_pos = origin_info._next;
            Block_Head temp_info;
            Leaf_Data temp_data;

            read_block(&temp_info, &temp_data, temp_pos);
            temp_info._last = new_pos;
            write_block(&temp_info, &temp_data, temp_pos);
            origin_info._next = new_pos;

            Block_Head new_info;
            Leaf_Data new_data;
            read_block(&new_info, &new_data, new_pos);

            off_t mid_pos = origin_info._size >> 1;

            for (off_t p = mid_pos, i = 0; p < origin_info._size; ++p, ++i) {
                new_data.val[i].first = origin_data.val[p].first;
                new_data.val[i].second = origin_data.val[p].second;
                ++new_info._size;
            }

            origin_info._size = mid_pos;

            insert_new_index(parent_info, parent_data, pos, new_pos, origin_data.val[mid_pos].first);

            write_block(&origin_info, &origin_data, pos);
            write_block(&new_info, &new_data, new_pos);
            write_block(&parent_info, &parent_data, parent_pos);

            return new_data.val[0].first;
        }

        bool check_parent(Block_Head& son_info) {

            off_t parent_pos, origin_pos = son_info._parent;
            Block_Head parent_info, origin_info;
            DATA parent_data, origin_data;

            read_block(&origin_info, &origin_data, origin_pos);

            if (origin_info._size < M)
                return false;

            if (origin_pos == bpt.root) {

                off_t root = newnode(0);
                bpt.root = root;
                update_bpt();
                read_block(&parent_info, &parent_data, root);

                origin_info._parent = root;
                ++parent_info._size;
                parent_data.val[0]._son = origin_pos;
                parent_pos = root;
            }

            else {
                read_block(&parent_info, &parent_data, origin_info._parent);
                parent_pos = parent_info._pos;
            }

            if (check_parent(origin_info)) {
                parent_pos = origin_info._parent;
                read_block(&parent_info, &parent_data, parent_pos);
            }

            auto new_pos = newnode(parent_pos);
            Block_Head new_info;
            DATA new_data;

            read_block(&new_info, &new_data, new_pos);

            off_t mid_pos = origin_info._size >> 1;
            for (off_t p = mid_pos + 1, i = 0; p < origin_info._size; ++p,++i) {
                if (origin_data.val[p]._son == son_info._pos) 
                    son_info._parent = new_pos;

                std::swap(new_data.val[i], origin_data.val[p]);
                ++new_info._size;
            }

            origin_info._size = mid_pos + 1;
            insert_new_index(parent_info, parent_data, origin_pos, new_pos, origin_data.val[mid_pos]._key);

            write_block(&origin_info, &origin_data, origin_pos);
            write_block(&new_info, &new_data, new_pos);
            write_block(&parent_info, &parent_data, parent_pos);

            return true;
        }

        void change_index(off_t l_parent, off_t l_son, const Key& new_key) {

            Block_Head parent_info;
            DATA parent_data;
            read_block(&parent_info, &parent_data, l_parent);

            if (parent_data.val[parent_info._size - 1]._son == l_son) {
                change_index(parent_info._parent, l_parent, new_key);
                return;
            }

            for (off_t now_pos = parent_info._size - 2;; --now_pos) {
                if (parent_data.val[now_pos]._son == l_son) {
                    parent_data.val[now_pos]._key = new_key;
                    break;
                }
            }

            write_block(&parent_info, &parent_data, l_parent);
        }

        void merge_node(Block_Head& l_info, DATA& l_data, Block_Head& r_info, DATA& r_data) {
            for (off_t p = l_info._size, i = 0; i < r_info._size; ++p, ++i) {
                l_data.val[p] = r_data.val[i];
            }

            l_data.val[l_info._size - 1]._key = adjust_node(r_info._parent, r_info._pos);
            l_info._size += r_info._size;
            write_block(&l_info, &l_data, l_info._pos);
        }


        void balance_node(Block_Head& info, DATA& data) {

            if (info._size >= M / 2) {
                write_block(&info, &data, info._pos);
                return;
            }

            if (info._pos == bpt.root && info._size <= 1) {
                bpt.root = data.val[0]._son;
                update_bpt();
                return;
            }

            else if (info._pos == bpt.root) {
                write_block(&info, &data, info._pos);
                return;
            }

            Block_Head parent_info, brother_info;
            DATA parent_data, brother_data;
            read_block(&parent_info, &parent_data, info._parent);
            off_t value_pos=0;

            //for (value_pos = 0; parent_data.val[value_pos]._son != info._pos; ++value_pos);
            while(parent_data.val[value_pos]._son != info._pos) ++value_pos;

            if (value_pos > 0) {
                read_block(&brother_info, &brother_data, parent_data.val[value_pos - 1]._son);
                brother_info._parent = info._parent;
                if (brother_info._size > M / 2) {
                    for (off_t p = info._size; p > 0; --p) 
                        data.val[p] = data.val[p - 1];
                    
                    data.val[0]._son = brother_data.val[brother_info._size - 1]._son;
                    data.val[0]._key = parent_data.val[value_pos - 1]._key;
                    parent_data.val[value_pos - 1]._key = brother_data.val[brother_info._size - 2]._key;

                    --brother_info._size;
                    ++info._size;

                    write_block(&brother_info, &brother_data, brother_info._pos);
                    write_block(&info, &data, info._pos);
                    write_block(&parent_info, &parent_data, parent_info._pos);

                    return;
                }

                else {
                    merge_node(brother_info, brother_data, info, data);
                    return;
                }
            }

            if (value_pos < parent_info._size - 1) {
                read_block(&brother_info, &brother_data, parent_data.val[value_pos + 1]._son);
                brother_info._parent = info._parent;

                if (brother_info._size > M / 2) {
                    data.val[info._size]._son = brother_data.val[0]._son;
                    data.val[info._size - 1]._key = parent_data.val[value_pos]._key;
                    parent_data.val[value_pos]._key = brother_data.val[0]._key;

                    for (off_t p = 1; p < brother_info._size; ++p) {
                        brother_data.val[p - 1] = brother_data.val[p];
                    }

                    --brother_info._size;
                    ++info._size;

                    write_block(&brother_info, &brother_data, brother_info._pos);
                    write_block(&info, &data, info._pos);
                    write_block(&parent_info, &parent_data, parent_info._pos);

                    return;
                }

                else {
                    merge_node(info, data, brother_info, brother_data);
                    return;
                }
            }
        }

        Key adjust_node(off_t pos, off_t removed_son) {
            Block_Head info;
            DATA data;
            off_t now_pos;
            read_block(&info, &data, pos);

            for (now_pos = 0; data.val[now_pos]._son != removed_son; ++now_pos);

            Key ans = data.val[now_pos - 1]._key;
            data.val[now_pos - 1]._key = data.val[now_pos]._key;

            for (; now_pos < info._size - 1; ++now_pos) {
                data.val[now_pos] = data.val[now_pos + 1];
            }
            --info._size;

            balance_node(info, data);
            return ans;
        }

        void merge_leaf(Block_Head& l_info, Leaf_Data& l_data, Block_Head& r_info, Leaf_Data& r_data) {

            for (off_t p = l_info._size, i = 0; i < r_info._size; ++p, ++i) {
                l_data.val[p].first = r_data.val[i].first;
                l_data.val[p].second = r_data.val[i].second;
            }

            l_info._size += r_info._size;
            adjust_node(r_info._parent, r_info._pos);
            l_info._next = r_info._next;

            Block_Head temp_info;
            Leaf_Data temp_data;
            read_block(&temp_info, &temp_data, r_info._next);
            temp_info._last = l_info._pos;

            write_block(&temp_info, &temp_data, temp_info._pos);
            write_block(&l_info, &l_data, l_info._pos);
        }

        void balance_leaf(Block_Head& leaf_info, Leaf_Data& leaf_data) {

            if (leaf_info._size >= L / 2) {
                write_block(&leaf_info, &leaf_data, leaf_info._pos);
                return;
            }

            else if (leaf_info._pos == bpt.root) {
                if (leaf_info._size == 0) {
                    Block_Head temp_info;
                    Leaf_Data temp_data;
                    read_block(&temp_info, &temp_data, bpt.head);
                    temp_info._next = bpt.rear;

                    write_block(&temp_info, &temp_data, bpt.head);
                    read_block(&temp_info, &temp_data, bpt.rear);
                    temp_info._last = bpt.head;
                    write_block(&temp_info, &temp_data, bpt.rear);
                    return;
                }
                write_block(&leaf_info, &leaf_data, leaf_info._pos);
                return;
            }

            Block_Head brother_info, parent_info;
            Leaf_Data brother_data;
            DATA parent_data;

            read_block(&parent_info, &parent_data, leaf_info._parent);
            off_t node_pos;
            for (node_pos = 0; node_pos < parent_info._size; ++node_pos) {
                if (parent_data.val[node_pos]._son == leaf_info._pos)
                    break;
            }
            //左
            if (node_pos > 0) {

                read_block(&brother_info, &brother_data, leaf_info._last);
                brother_info._parent = leaf_info._parent;

                if (brother_info._size > L / 2) {
                    for (off_t p = leaf_info._size; p > 0; --p) {
                        leaf_data.val[p].first = leaf_data.val[p - 1].first;
                        leaf_data.val[p].second = leaf_data.val[p - 1].second;
                    }
                    leaf_data.val[0].first = brother_data.val[brother_info._size - 1].first;
                    leaf_data.val[0].second = brother_data.val[brother_info._size - 1].second;

                    --brother_info._size;
                    ++leaf_info._size;

                    change_index(brother_info._parent, brother_info._pos, leaf_data.val[0].first);

                    write_block(&brother_info, &brother_data, brother_info._pos);
                    write_block(&leaf_info, &leaf_data, leaf_info._pos);
                    return;
                }

                else {
                    merge_leaf(brother_info, brother_data, leaf_info, leaf_data);
                    return;
                }
            }

            //右
            if (node_pos < parent_info._size - 1) {
                read_block(&brother_info, &brother_data, leaf_info._next);
                brother_info._parent = leaf_info._parent;

                if (brother_info._size > L / 2) {
                    leaf_data.val[leaf_info._size].first = brother_data.val[0].first;
                    leaf_data.val[leaf_info._size].second = brother_data.val[0].second;

                    for (off_t p = 1; p < brother_info._size; ++p) {
                        brother_data.val[p - 1].first = brother_data.val[p].first;
                        brother_data.val[p - 1].second = brother_data.val[p].second;
                    }

                    ++leaf_info._size;
                    --brother_info._size;

                    change_index(leaf_info._parent, leaf_info._pos, brother_data.val[0].first);

                    write_block(&leaf_info, &leaf_data, leaf_info._pos);
                    write_block(&brother_info, &brother_data, brother_info._pos);

                    return;
                }
                else {
                    merge_leaf(leaf_info, leaf_data, brother_info, brother_data);
                    return;
                }
            }
        }

        void check_file() {

            if (!file) {
                file = fopen(ADDRESS, "wb+");
                update_bpt();

                off_t node_head = bpt.block_cnt;
                off_t node_rear = bpt.block_cnt + 1;
                bpt.head = node_head;
                bpt.rear = node_rear;

                newleafnode(0, 0, node_rear);
                newleafnode(0, node_head, 0);
                return;
            }
            char buff[BLOCKSIZE] = { 0 };
            Read(buff, BLOCKSIZE, 0);
            memcpy(&bpt, buff, sizeof(bpt));
        }

    public:

        typedef pair<const Key, Value> value_type;


        class const_iterator;
        class iterator {

            friend class sjtu::BTree<Key, Value, Compare>::const_iterator;
            friend iterator sjtu::BTree<Key, Value, Compare>::begin();
            friend iterator sjtu::BTree<Key, Value, Compare>::end();
            friend iterator sjtu::BTree<Key, Value, Compare>::find(const Key&);
            friend pair<iterator, OperationResult> sjtu::BTree<Key, Value, Compare>::insert(const Key&, const Value&);

        private:
            // Your private members go here
            BTree* now_bpt = nullptr;
            Block_Head block_info;
            off_t now_pos = 0;

        public:

            bool modify(const Value& value) {

                Block_Head info;
                Leaf_Data leaf_data;

                read_block(&info, &leaf_data, block_info._pos);
                leaf_data.val[now_pos].second = value;
                write_block(&info, &leaf_data, block_info._pos);

                return true;
            }

            iterator() {

                // TODO Default Constructor

            }

            iterator(const iterator& other) {

                // TODO Copy Constructor
                now_bpt = other.now_bpt;
                block_info = other.block_info;
                now_pos = other.now_pos;
            }

            // Return a new iterator which points to the n-next elements

            iterator operator++(int) {

                // Todo iterator++

                auto temp = *this;
                ++now_pos;

                if (now_pos >= block_info._size) {
                    char buff[BLOCKSIZE] = { 0 };
                    Read(buff, BLOCKSIZE, block_info._next);
                    memcpy(&block_info, buff, sizeof(block_info));
                    now_pos = 0;
                }
                return temp;
            }

            iterator& operator++() {

                // Todo ++iterator

                ++now_pos;
                if (now_pos >= block_info._size) {
                    char buff[BLOCKSIZE] = { 0 };
                    Read(buff, BLOCKSIZE, block_info._next);
                    memcpy(&block_info, buff, sizeof(block_info));
                    now_pos = 0;
                }
                return *this;
            }

            iterator operator--(int) {

                // Todo iterator--

                auto temp = *this;

                if (now_pos == 0) {
                    char buff[BLOCKSIZE] = { 0 };
                    Read(buff, BLOCKSIZE, block_info._last);
                    memcpy(&block_info, buff, sizeof(block_info));
                    now_pos = block_info._size - 1;
                }
                else --now_pos;
                return temp;
            }

            iterator& operator--() {

                // Todo --iterator

                if (now_pos == 0) {
                    char buff[BLOCKSIZE] = { 0 };
                    Read(buff, BLOCKSIZE, block_info._last);
                    memcpy(&block_info, buff, sizeof(block_info));
                    now_pos = block_info._size - 1;
                }
                else --now_pos;
                return *this;
            }

            // Overloaded of operator '==' and '!='
            // Check whether the iterators are same

            value_type operator*() const {

                // Todo operator*, return the <K,V> of iterator

                if (now_pos >= block_info._size)
                    throw invalid_iterator();

                char buff[BLOCKSIZE] = { 0 };
                Read(buff, BLOCKSIZE, block_info._pos);
                Leaf_Data leaf_data;
                memcpy(&leaf_data, buff + BGSIZE, sizeof(leaf_data));
                value_type ans(leaf_data.val[now_pos].first,leaf_data.val[now_pos].second);
                return ans;
            }

            bool operator==(const iterator& rhs) const {

                // Todo operator ==
                bool flag;
                flag=(now_bpt == rhs.now_bpt && block_info._pos == rhs.block_info._pos && now_pos == rhs.now_pos);
                return flag;
            }

            bool operator==(const const_iterator& rhs) const {

                // Todo operator ==
                bool flag;
                flag=(block_info._pos == rhs.block_info._pos && now_pos == rhs.now_pos);
                return flag;
            }

            bool operator!=(const iterator& rhs) const {
                // Todo operator !=
                return now_bpt != rhs.now_bpt|| block_info._pos != rhs.block_info._pos|| now_pos != rhs.now_pos;
            }

            bool operator!=(const const_iterator& rhs) const {
                // Todo operator !=
                return block_info._pos != rhs.block_info._pos || now_pos != rhs.now_pos;
            }
        };

        class const_iterator {
            // it should has similar member method as iterator.
            //  and it should be able to construct from an iterator.

            friend class sjtu::BTree<Key, Value, Compare>::iterator;
            friend const_iterator sjtu::BTree<Key, Value, Compare>::cbegin() const;
            friend const_iterator sjtu::BTree<Key, Value, Compare>::cend() const;
            friend const_iterator sjtu::BTree<Key, Value, Compare>::find(const Key&) const;

        private:
            // Your private members go here
            Block_Head block_info;
            off_t now_pos = 0;
        public:
            const_iterator() {

                // TODO

            }

            const_iterator(const const_iterator& other) {
                // TODO
                block_info = other.block_info;
                now_pos = other.now_pos;
            }

            const_iterator(const iterator& other) {

                // TODO
                block_info = other.block_info;
                now_pos = other.now_pos;
            }

            // And other methods in iterator, please fill by yourself.
            // Return a new iterator which points to the n-next elements

            const_iterator operator++(int) {

                // Todo iterator++

                auto temp = *this;
                ++now_pos;
                if (now_pos >= block_info._size) {
                    char buff[BLOCKSIZE] = { 0 };
                    Read(buff, BLOCKSIZE, block_info._next);
                    memcpy(&block_info, buff, sizeof(block_info));
                    now_pos = 0;
                }
                return temp;
            }

            const_iterator& operator++() {

                // Todo ++iterator
                ++now_pos;
                if (now_pos >= block_info._size) {
                    char buff[BLOCKSIZE] = { 0 };
                    Read(buff, BLOCKSIZE, block_info._next);
                    memcpy(&block_info, buff, sizeof(block_info));
                    now_pos = 0;
                }
                return *this;
            }

            const_iterator operator--(int) {

                // Todo iterator--

                auto temp = *this;
                if (now_pos == 0) {
                    char buff[BLOCKSIZE] = { 0 };
                    Read(buff, BLOCKSIZE, block_info._last);
                    memcpy(&block_info, buff, sizeof(block_info));
                    now_pos = block_info._size - 1;
                }
                else --now_pos;
                return temp;
            }

            const_iterator& operator--() {

                // Todo --iterator

                if (now_pos == 0) {
                    char buff[BLOCKSIZE] = { 0 };
                    Read(buff, BLOCKSIZE, block_info._last);
                    memcpy(&block_info, buff, sizeof(block_info));
                    now_pos = block_info._size - 1;
                }

                else --now_pos;
                return *this;
            }

            // Overloaded of operator '==' and '!='
            // Check whether the iterators are same

            value_type operator*() const {

                // Todo operator*, return the <K,V> of iterator

                if (now_pos >= block_info._size)
                    throw invalid_iterator();

                char buff[BLOCKSIZE] = { 0 };
                Read(buff, BLOCKSIZE, block_info._pos);
                Leaf_Data leaf_data;
                memcpy(&leaf_data, buff + BGSIZE, sizeof(leaf_data));
                value_type ans(leaf_data.val[now_pos].first, leaf_data.val[now_pos].second);
                return ans;
            }

            bool operator==(const iterator& rhs) const {

                // Todo operator ==
                return block_info._pos == rhs.block_info._pos && now_pos == rhs.now_pos;
            }

            bool operator==(const const_iterator& rhs) const {
                // Todo operator ==
                return block_info._pos == rhs.block_info._pos && now_pos == rhs.now_pos;
            }

            bool operator!=(const iterator& rhs) const {

                // Todo operator !=
                return block_info._pos != rhs.block_info._pos || now_pos != rhs.now_pos;
            }

            bool operator!=(const const_iterator& rhs) const {
                // Todo operator !=
                return block_info._pos != rhs.block_info._pos || now_pos != rhs.now_pos;
            }
        };

        // Default Constructor and Copy Constructor

        BTree() {
            // Todo Default
            file = fopen(ADDRESS, "rb+");
            if (!file) {
                file = fopen(ADDRESS, "wb+");
                update_bpt();

                off_t node_head = bpt.block_cnt;
                off_t node_rear = bpt.block_cnt + 1;

                bpt.head = node_head;
                bpt.rear = node_rear;

                newleafnode(0, 0, node_rear);
                newleafnode(0, node_head, 0);
                return;
            }
            char buff[BLOCKSIZE] = { 0 };
            Read(buff, BLOCKSIZE, 0);
            memcpy(&bpt, buff, sizeof(bpt));
        }

        BTree(const BTree& other) {

            // Todo Copy

            file = fopen(ADDRESS, "rb+");
            bpt.block_cnt = other.bpt.block_cnt;
            bpt.head = other.bpt.head;
            bpt.rear = other.bpt.rear;
            bpt.root = other.bpt.root;
            bpt._size = other.bpt._size;
        }

        BTree& operator=(const BTree& other) {

            // Todo Assignment
            file = fopen(ADDRESS, "rb+");
            bpt.block_cnt = other.bpt.block_cnt;
            bpt.head = other.bpt.head;
            bpt.rear = other.bpt.rear;
            bpt.root = other.bpt.root;
            bpt._size = other.bpt._size;
            return *this;
        }

        ~BTree() {

            // Todo Destructor
            fclose(file);
        }

        // Insert: Insert certain Key-Value into the database
        // Return a pair, the first of the pair is the iterator point to the new
        // element, the second of the pair is Success if it is successfully inserted

        pair<iterator, OperationResult> insert(const Key& key, const Value& value) {

            // TODO insert function

            check_file();
            if (empty()) {
                auto root = newleafnode(0, bpt.head, bpt.rear);
                Block_Head temp_info;
                Leaf_Data temp_data;
                read_block(&temp_info, &temp_data, bpt.head);
                temp_info._next = root;

                write_block(&temp_info, &temp_data, bpt.head);
                read_block(&temp_info, &temp_data, bpt.rear);
                temp_info._last = root;
                write_block(&temp_info, &temp_data, bpt.rear);
                read_block(&temp_info, &temp_data, root);

                ++temp_info._size;
                temp_data.val[0].first = key;
                temp_data.val[0].second = value;
                write_block(&temp_info, &temp_data, root);

                ++bpt._size;
                bpt.root = root;
                update_bpt();

                pair<iterator, OperationResult> ans(begin(), Success);
                return ans;
            }

            char buff[BLOCKSIZE] = { 0 };
            off_t now_pos = bpt.root, now_parent = 0;
            while (true) {

                Read(buff, BLOCKSIZE, now_pos);
                Block_Head temp;
                memcpy(&temp, buff, sizeof(temp));

                if (now_parent != temp._parent) {
                    temp._parent = now_parent;
                    memcpy(buff, &temp, sizeof(temp));
                    Write(buff, BLOCKSIZE, now_pos);
                }

                if (temp.isLeaf) {
                    break;
                }

                DATA data;
                memcpy(&data, buff + BGSIZE, sizeof(data));
                off_t son_pos = temp._size - 1;

                for (; son_pos > 0; --son_pos) {
                    if (!(data.val[son_pos - 1]._key > key)) {
                        break;
                    }
                }

                now_parent = now_pos;
                now_pos = data.val[son_pos]._son;
                now_pos = now_pos;
            }

            Block_Head info;
            memcpy(&info, buff, sizeof(info));
            Leaf_Data leaf_data;
            memcpy(&leaf_data, buff + BGSIZE, sizeof(leaf_data));

            for (off_t value_pos = 0;; ++value_pos) {
                if (value_pos < info._size && leaf_data.val[value_pos].first==key) {
                    return pair<iterator, OperationResult>(end(), Fail);
                }

                if (value_pos >= info._size || leaf_data.val[value_pos].first > key) { //插在此结点前

                    if (info._size >= L) {
                        off_t now_key = split_leaf_node(now_pos, info, leaf_data);
                        if (key > now_key) {
                            now_pos = info._next;
                            value_pos -= info._size;
                            read_block(&info, &leaf_data, now_pos);
                        }
                    }

                    for (off_t p = info._size - 1; p >= value_pos; --p) {
                        leaf_data.val[p + 1].first = leaf_data.val[p].first;
                        leaf_data.val[p + 1].second = leaf_data.val[p].second;

                        if (p == value_pos)  break;
                    }

                    leaf_data.val[value_pos].first = key;
                    leaf_data.val[value_pos].second = value;
                    ++info._size;
                    write_block(&info, &leaf_data, now_pos);
                    iterator ans;
                    ans.block_info = info;
                    ans.now_bpt = this;
                    ans.now_pos = value_pos;

                    ++bpt._size;
                    update_bpt();

                    pair<iterator, OperationResult> ANS(ans, Success);
                    return ANS;
                }
            }
        }

        // Erase: Erase the Key-Value

        // Return Success if it is successfully erased

        // Return Fail if the key doesn't exist in the database

        OperationResult erase(const Key& key) {

            // TODO erase function
            check_file();
            if (empty()) {
                return Fail;
            }

            char buff[BLOCKSIZE] = { 0 };
            off_t now_pos = bpt.root, now_parent = 0;
            while (true) {
                Read(buff, BLOCKSIZE, now_pos);
                Block_Head temp;
                memcpy(&temp, buff, sizeof(temp));

                if (now_parent != temp._parent) {
                    temp._parent = now_parent;
                    memcpy(buff, &temp, sizeof(temp));
                    Write(buff, BLOCKSIZE, now_pos);
                }

                if (temp.isLeaf)  break;

                DATA data;
                memcpy(&data, buff + BGSIZE, sizeof(data));

                off_t son_pos = temp._size - 1;

                for (; son_pos > 0; --son_pos) {
                    if (data.val[son_pos - 1]._key <= key) {
                        break;
                    }
                }
                now_parent = now_pos;
                now_pos = data.val[son_pos]._son;
            }

            Block_Head info;
            memcpy(&info, buff, sizeof(info));
            Leaf_Data leaf_data;
            memcpy(&leaf_data, buff + BGSIZE, sizeof(leaf_data));

            for (off_t value_pos = 0;; ++value_pos) {

                if (value_pos < info._size && leaf_data.val[value_pos].first==key) {
                    --info._size;

                    for (off_t p = value_pos; p < info._size; ++p) {
                        leaf_data.val[p].first = leaf_data.val[p + 1].first;
                        leaf_data.val[p].second = leaf_data.val[p + 1].second;
                    }
                    balance_leaf(info, leaf_data);
                    --bpt._size;
                    update_bpt();
                    return Success;
                }

                if (value_pos >= info._size || leaf_data.val[value_pos].first > key) {
                    return Fail;
                }
            }
        }

        iterator begin() {
            check_file();
            iterator ans;

            char buff[BLOCKSIZE] = { 0 };
            Read(buff, BLOCKSIZE, bpt.head);
            Block_Head block_head;
            memcpy(&block_head, buff, sizeof(block_head));

            ans.block_info = block_head;
            ans.now_bpt = this;
            ans.now_pos = 0;
            ++ans;
            return ans;
        }

        const_iterator cbegin() const {
            const_iterator ans;
            char buff[BLOCKSIZE] = { 0 };
            Read(buff, BLOCKSIZE, bpt.head);
            Block_Head block_head;
            memcpy(&block_head, buff, sizeof(block_head));

            ans.block_info = block_head;
            ans.now_pos = 0;
            ++ans;
            return ans;
        }

        // Return a iterator to the end(the next element after the last)

        iterator end() {
            check_file();

            iterator ans;
            char buff[BLOCKSIZE] = { 0 };
            Read(buff, BLOCKSIZE, bpt.rear);
            Block_Head block_head;
            memcpy(&block_head, buff, sizeof(block_head));

            ans.block_info = block_head;
            ans.now_bpt = this;
            ans.now_pos = 0;
            return ans;
        }

        const_iterator cend() const {
            const_iterator ans;
            char buff[BLOCKSIZE] = { 0 };
            Read(buff, BLOCKSIZE, bpt.rear);
            Block_Head block_head;
            memcpy(&block_head, buff, sizeof(block_head));

            ans.block_info = block_head;
            ans.now_pos = 0;
            return ans;
        }

        // Check whether this BTree is empty

        bool empty() const {
            if (!file) return true;
            return bpt._size == 0;
        }

        // Return the number of <K,V> pairs

        off_t size() const {
            if (!file) return 0;
            return bpt._size;
        }

        // Clear the BTree

        void clear() {

            file = fopen(ADDRESS, "w");

        }

        // Return the value refer to the Key(key)

        Value at(const Key& key) {
            if (empty())   throw container_is_empty();

            char buff[BLOCKSIZE] = { 0 };
            off_t now_pos = bpt.root, now_parent = 0;
            while (true) {

                Read(buff, BLOCKSIZE, now_pos);
                Block_Head temp;
                memcpy(&temp, buff, sizeof(temp));

                if (now_parent != temp._parent) {
                    temp._parent = now_parent;
                    memcpy(buff, &temp, sizeof(temp));
                    Write(buff, BLOCKSIZE, now_pos);
                }

                if (temp.isLeaf)  break;

                DATA data;
                memcpy(&data, buff + BGSIZE, sizeof(data));
                off_t son_pos = temp._size - 1;

                for (; son_pos > 0; --son_pos) {
                    if (!(data.val[son_pos - 1]._key > key))
                        break;
                }
                now_pos = data.val[son_pos]._son;
            }

            Block_Head info;
            memcpy(&info, buff, sizeof(info));
            Leaf_Data leaf_data;
            memcpy(&leaf_data, buff + BGSIZE, sizeof(leaf_data));

            for (off_t value_pos = 0;; ++value_pos) {
                if (value_pos < info._size && (!(leaf_data.val[value_pos].first<key || leaf_data.val[value_pos].first>key))) {
                    return leaf_data.val[value_pos].second;
                }

                if (value_pos >= info._size || leaf_data.val[value_pos].first > key) {
                    throw index_out_of_bound();
                }
            }
        }

        /**

         * Returns the number of elements with key

         *   that compares equivalent to the specified argument,

         * The default method of check the equivalence is !(a < b || b > a)

         */

        off_t count(const Key& key) const {
            return find(key) == cend() ? 0 : 1;
        }

        /**

         * Finds an element with key equivalent to key.

         * key value of the element to search for.

         * Iterator to an element with key equivalent to key.

         *   If no such element is found, past-the-end (see end()) iterator is

         * returned.

         */

        iterator find(const Key& key) {
            if (empty()) {
                return end();
            }

            char buff[BLOCKSIZE] = { 0 };
            off_t now_pos = bpt.root, now_parent = 0;

            while (true) {
                Read(buff, BLOCKSIZE, now_pos);
                Block_Head temp;
                memcpy(&temp, buff, sizeof(temp));

                if (now_parent != temp._parent) {
                    temp._parent = now_parent;
                    memcpy(buff, &temp, sizeof(temp));
                    Write(buff, BLOCKSIZE, now_pos);
                }

                if (temp.isLeaf) {
                    break;
                }

                DATA data;
                memcpy(&data, buff + BGSIZE, sizeof(data));
                off_t son_pos = temp._size - 1;

                for (; son_pos > 0; --son_pos) {
                    if (!(data.val[son_pos - 1]._key > key)) {
                        break;
                    }
                }
                now_pos = data.val[son_pos]._son;
            }

            Block_Head info;
            memcpy(&info, buff, sizeof(info));
            Leaf_Data leaf_data;
            memcpy(&leaf_data, buff + BGSIZE, sizeof(leaf_data));

            for (off_t value_pos = 0;; ++value_pos) {
                if (value_pos < info._size && leaf_data.val[value_pos].first==key) {
                    iterator ans;
                    ans.now_bpt = this;
                    ans.block_info = info;
                    ans.now_pos = value_pos;
                    return ans;
                }

                if (value_pos >= info._size || leaf_data.val[value_pos].first > key) {
                    return end();
                }
            }
        }

        const_iterator find(const Key& key) const {

            if (empty()) {
                return cend();
            }

            char buff[BLOCKSIZE] = { 0 };
            off_t now_pos = bpt.root, now_parent = 0;

            while (true) {
                Read(buff, BLOCKSIZE, now_pos);
                Block_Head temp;
                memcpy(&temp, buff, sizeof(temp));

                if (now_parent != temp._parent) {
                    temp._parent = now_parent;
                    memcpy(buff, &temp, sizeof(temp));
                    Write(buff, BLOCKSIZE, now_pos);
                }

                if (temp.isLeaf) {
                    break;
                }

                DATA data;
                memcpy(&data, buff + BGSIZE, sizeof(data));
                off_t son_pos = temp._size - 1;

                for (; son_pos > 0; --son_pos) {
                    if (!(data.val[son_pos - 1]._key > key))
                        break;
                }
                now_pos = data.val[son_pos]._son;
            }

            Block_Head info;
            memcpy(&info, buff, sizeof(info));
            Leaf_Data leaf_data;
            memcpy(&leaf_data, buff + BGSIZE, sizeof(leaf_data));

            for (off_t value_pos = 0;; ++value_pos) {
                if (value_pos < info._size && (!(leaf_data.val[value_pos].first<key || leaf_data.val[value_pos].first>key))) {
                    const_iterator ans;
                    ans.block_info = info;
                    ans.now_pos = value_pos;
                    return ans;
                }

                if (value_pos >= info._size || leaf_data.val[value_pos].first > key) {
                    return cend();
                }
            }
        }
    };

    template <typename Key, typename Value, typename Compare> FILE* BTree<Key, Value, Compare>::file = nullptr;

}  // namespace sjtu

