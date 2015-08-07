/*******************************************************************************
 * c7a/api/write.hpp
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Matthias Stumpp <mstumpp@gmail.com>
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 * Copyright (C) 2015 Alexander Noe <aleexnoe@gmail.com>
 *
 * This file has no license. Only Chunk Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef C7A_API_WRITE_SINGLE_FILE_HEADER
#define C7A_API_WRITE_SINGLE_FILE_HEADER

#include <c7a/api/action_node.hpp>
#include <c7a/api/dia.hpp>
#include <c7a/core/stage_builder.hpp>
#include <c7a/data/file.hpp>

#include <fstream>
#include <string>

namespace c7a {
namespace api {

//! \addtogroup api Interface
//! \{

template <typename ValueType, typename ParentDIARef>
class WriteLinesNode : public ActionNode
{
    static const bool debug = true;

public:
    using Super = ActionNode;
    using Super::result_file_;
    using Super::context_;

    WriteLinesNode(const ParentDIARef& parent,
              const std::string& path_out,
              StatsNode* stats_node)
        : ActionNode(parent.ctx(), { parent.node() },
                     "WriteSingleFile", stats_node),
          path_out_(path_out),
          file_(path_out_),
          writer_(result_file_.GetWriter()),
          emit_(file_)
    {
        sLOG << "Creating write node.";

        auto pre_op_fn = [=](ValueType input) {
                             PreOp(input);
                         };
        // close the function stack with our pre op and register it at parent
        // node for output
        auto lop_chain = parent.stack().push(pre_op_fn).emit();
        parent.node()->RegisterChild(lop_chain);
        
    }

    void PreOp(ValueType input) {
        writer_(input);
        emit_(input);
    }

    //! Closes the output file
    void Execute() override {
        writer_.Close();

        data::File::Reader reader = result_file_.GetReader();
            
        for (size_t i = 0; i < result_file_.NumItems(); ++i) {
            sLOG << "element: " << reader.Next<ValueType>();
        }
        
        sLOG << "size: " << result_file_.TotalSize();
        sLOG << "closing file" << path_out_;
        emit_.Close();
    }

    void Dispose() override { }

    /*!
     * Returns "[WriteNode]" and its id as a string.
     * \return "[WriteNode]"
     */
    std::string ToString() override {
        return "[WriteNode] Id:" + result_file_.ToString();
    }

protected:
    //! OutputEmitter let's you write to files. Each element is written
    //! using ostream.
    class OutputEmitter
    {
    public:
        explicit OutputEmitter(std::ofstream& file)
            : out_(file) { }

        //! write item out using ostream formatting / serialization.
        void operator () (const ValueType& v) {
            out_ << v;
        }

        //! Flushes and closes the block (cannot be undone)
        //! No further emitt operations can be done afterwards.
        void Close() {
            assert(!closed_);
            closed_ = true;
            out_.close();
        }

        //! Writes the data to the target without closing the emitter
        void Flush() {
            out_.flush();
        }

    private:
        //! output stream
        std::ofstream& out_;

        //! whether the output stream is closed.
        bool closed_ = false;
    };

private:
    //! Path of the output file.
    std::string path_out_;

    //! File to write to
    std::ofstream file_;

    //! File writer used.
    data::File::Writer writer_;

    //! Emitter to file
    OutputEmitter emit_;
};

template <typename ValueType, typename Stack>
void DIARef<ValueType, Stack>::WriteLines(
    const std::string& filepath) const {

    using WriteResultNode = WriteLinesNode<ValueType, DIARef>;

    StatsNode* stats_node = AddChildStatsNode("Write", "Action");
    auto shared_node =
        std::make_shared<WriteResultNode>(*this,
                                          filepath,
                                          stats_node);

    core::StageBuilder().RunScope(shared_node.get());
}

//! \}

} // namespace api
} // namespace c7a

#endif // !C7A_API_WRITE_HEADER

/******************************************************************************/
