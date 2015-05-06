/*******************************************************************************
 * c7a/api/dia.hpp
 *
 * Interface for Operations, holds pointer to node and lambda from node to state
 ******************************************************************************/

#ifndef C7A_API_DIA_HEADER
#define C7A_API_DIA_HEADER

#include <functional>
#include <vector>
#include <stack>
#include <iostream>
#include <fstream>
#include <cassert>
#include <memory>
#include <unordered_map>
#include <string>

#include "dia_node.hpp"
#include "function_traits.hpp"
#include "lop_node.hpp"
#include "zip_node.hpp"
#include "read_node.hpp"
#include "reduce_node.hpp"
#include "context.hpp"

namespace c7a {
//! \addtogroup api Interface
//! \{

/*!
 * DIA is the interface between the user and the c7a framework. A DIA can be
 * imagined as an immutable array, even though the data does not need to be
 * materialized at all. A DIA contains a pointer to a DIANode of type T,
 * which represents the state after the previous DOp or Action. Additionally, a DIA
 * stores the local lambda function of type L, which can transform
 * elements of the DIANode to elements of this DIA. DOps/Actions create a DIA
 * and a new DIANode, to which the DIA links to. LOps only create a new DIA, which
 * link to the previous DIANode. The types T and L are inferred from the
 * user-defined function given through the operation.
 *
 * \tparam T Type of elements in this DIA.
 * \tparam L Type of the lambda function to transform elements from the previous
 *  DIANode to elements of this DIA.
 */
template <typename T, typename Stack>
class DIA
{
    friend class Context;

public:
    /*!
     * Constructor of a new DIA with a pointer to a DIANode and a lambda function
     * from the DIANode to this DIA.
     *
     * \param node Pointer to the last DIANode, DOps and Actions create a new DIANode,
     * LOps link to the DIANode of the previous DIA.
     *
     * \param lambda Function which can transform elements from the DIANode to elements
     * of this DIA.
     */
    DIA(DIANode<T>* node, Stack stack) : node_(node), local_stack_(stack) { }

    friend void swap(DIA& first, DIA& second)
    {
        using std::swap;
        swap(first.data_, second.data_);
    }

    DIA& operator = (DIA rhs)
    {
        swap(*this, rhs);
        return *this;
    }

    /*!
     * Returns a pointer to the according DIANode.
     */
    DIANode<T> * get_node()
    {
        return node_;
    }

    /*!
     * Map is a LOp, which maps this DIA according to the map_fn given by the user.
     * The map_fn maps each element of L's result type to one other element of a possibly different
     * type. The DIA returned by Map has the same type T. The lambda function of the returned DIA
     * is this DIA's local_lambda chained with map_fn. Therefore the type L of the returned DIA
     * is a lambda function from T to the result type of map_fn.
     *
     * \tparam map_fn_t Type of the map function. The type of the returned DIA is deducted from this type.
     *
     * \param map_fn Map function of type map_fn_t, which maps each element to an element of a possibly
     * different type.
     *
     */
    template <typename map_fn_t>
    auto Map(const map_fn_t &map_fn) {
        using map_arg_t
                  = typename FunctionTraits<map_fn_t>::template arg<0>;
        using map_result_t
                  = typename FunctionTraits<map_fn_t>::result_type;
        auto conv_map_fn = [ = ](map_arg_t input, std::function<void(map_result_t)> emit_func) {
                               emit_func(map_fn(input));
                           };

        auto new_stack = local_stack_.push(conv_map_fn);
        return DIA<T, decltype(new_stack)>(node_, new_stack);
    }

    /*!
     * FlatMap is a LOp, which maps this DIA according to the flatmap_fn given by the user.
     * The flatmap_fn maps each element of type L's result to elements of a possibly different
     * type. The flatmap_fn has an emitter function as it's second parameter. This emitter is called
     * once for each element to be emitted. The DIA returned by FlatMap has the same type T. The
     * lambda function of the returned DIA is this DIA's local_lambda chained with flatmap_fn.
     * Therefore the type L of the returned DIA is a lambda function from T to the result type of flatmap_fn.
     *
     * \tparam flatmap_fn_t Type of the map function. The type of the returned DIA is deducted from this type
     *
     * \param flatmap_fn Map function of type map_fn_t, which maps each element to an element of a possibly
     * different type.
     */
    template <typename flatmap_fn_t>
    auto FlatMap(const flatmap_fn_t &flatmap_fn) {
        auto new_stack = local_stack_.push(flatmap_fn);
        return DIA<T, decltype(new_stack)>(node_, new_stack);
    }

    /*!
     * Reduce is a DOp, which groups elements of the DIA with the key_extractor and reduces each
     * key-bucket to a single element using the associative reduce_function. The reduce_function
     * defines how two elements can be reduced to a single element of equal type. As Reduce is a DOp,
     * it creates a new DIANode with the type of L's result. The DIA returned by Reduce links to this newly
     * created DIANode. The local_lambda of the returned DIA consists of the reduce_function, as a reduced
     * element can directly be chained to the following LOps.
     *
     * \tparam key_extr_fn_t Type of the key_extractor function. This is a function from L's result type to a
     * possibly different key type. The key_extractor function is equal to a map function.
     *
     * \tparam reduce_fn_t Type of the reduce_function. This is a function reducing two elements of L's result type
     * to a single element of equal type.
     *
     * \param key_extractor Key extractor function, which maps each element to a key of possibly different type.
     *
     * \param reduce_function Reduce function, which defines how the key buckets are reduced to a
     * single element. This function is applied associative but not necessarily commutative.
     *
     */
    template <typename key_extr_fn_t, typename reduce_fn_t>
    auto Reduce(const key_extr_fn_t &key_extractor, const reduce_fn_t &reduce_function) {
        using dop_result_t
                  = typename FunctionTraits<reduce_fn_t>::result_type;
        using ReduceResultNode
                  = ReduceNode<T, decltype(local_stack_), key_extr_fn_t, reduce_fn_t>;

        ReduceResultNode* reduce_node
            = new ReduceResultNode(node_->get_data_manager(),
                                   { node_ },
                                   local_stack_,
                                   key_extractor,
                                   reduce_function);

        auto reduce_stack = reduce_node->ProduceStack();
        return DIA<dop_result_t, decltype(reduce_stack)>
                   (reduce_node, reduce_stack);
    }

     /*!
      * Zip is a DOp, which Zips two DIAs in style of functional programming. The zip_function is used to
      * zip the i-th elements of both input DIAs together to form the i-th element of the output DIA. The
      * type of the output DIA can be inferred from the zip_function.
      *
      * \tparam zip_fn_t Type of the zip_function. This is a function with two input elements, both of the
      * local type, and one output element, which is the type of the Zip node.
      *
      * \param zip_fn Zip function, which zips two elements together
      *
      * \param Second DIA, which is zipped together with the original DIA.
      */
    template <typename zip_fn_t>
    auto Zip(const zip_fn_t &zip_fn, auto second_dia) {
        using zip_result_t
                  = typename FunctionTraits<zip_fn_t>::result_type;
        using ZipResultNode
            = TwoZipNode<typename FunctionTraits<zip_fn_t>::result_type,
                         decltype(local_stack_), decltype(second_dia.get_local_stack()), zip_fn_t>;

        ZipResultNode* zip_node
            = new ZipResultNode(node_->get_data_manager(),
                                { node_, second_dia.get_node() },
                                local_stack_,
                                second_dia.get_local_stack(),
                                zip_fn);

        auto zip_stack = zip_node->ProduceStack();
        return DIA<zip_result_t, decltype(zip_stack)>
                   (zip_node, zip_stack);
    }


    /*!
     * Returns Chuck Norris!
     *
     * \return Chuck Norris
     */
    const std::vector<T> & evil_get_data() const
    {
        return std::vector<T>{ T() };
    }

    /*!
     * Returns the string which defines the DIANode node_.
     *
     * \return The string of node_
     */
    std::string NodeString()
    {
        return node_->ToString();
    }

    /*!
     * Prints the DIANode and all it's children recursively. The printing is
     * performed tree-style.
     */
    void PrintNodes()
    {
        using BasePair = std::pair<DIABase*, int>;
        std::stack<BasePair> dia_stack;
        dia_stack.push(std::make_pair(node_, 0));
        while (!dia_stack.empty()) {
            auto curr = dia_stack.top();
            auto node = curr.first;
            int depth = curr.second;
            dia_stack.pop();
            auto is_end = true;
            if (!dia_stack.empty()) is_end = dia_stack.top().second < depth;
            for (int i = 0; i < depth - 1; ++i) {
                std::cout << "│   ";
            }
            if (is_end && depth > 0) std::cout << "└── ";
            else if (depth > 0) std::cout << "├── ";
            std::cout << node->ToString() << std::endl;
            auto children = node->get_childs();
            for (auto c : children) {
                dia_stack.push(std::make_pair(c, depth + 1));
            }
        }
    }

    Stack & get_local_stack() {
        return local_stack_;
    }

private:
    //! The DIANode which DIA points to. The node represents the latest DOp or Action performed previously.
    DIANode<T>* node_;
    //! The local function stack, which stores the chained lambda function from the last DIANode to this DIA.
    Stack local_stack_;
};

//! \}

template <typename read_fn_t>
auto ReadFromFileSystem(Context & ctx, std::string filepath,
                        const read_fn_t &read_fn) {
    (void)filepath;      //TODO remove | to supress warning
    using read_result_t = typename FunctionTraits<read_fn_t>::result_type;
    using ReadResultNode = ReadNode<read_result_t, read_fn_t>;

    ReadResultNode* read_node
        = new ReadResultNode(ctx,
                             { },
                             read_fn,
                             filepath);

    auto read_stack = read_node->ProduceStack();
    return DIA<read_result_t, decltype(read_stack)>
               (read_node, read_stack);
}
} // namespace c7a

#endif // !C7A_API_DIA_HEADER

/******************************************************************************/
