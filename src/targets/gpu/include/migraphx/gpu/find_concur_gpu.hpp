#ifndef MIGRAPHX_GUARD_RTGLIB_FIND_CONCUR_GPU_HPP
#define MIGRAPHX_GUARD_RTGLIB_FIND_CONCUR_GPU_HPP

#include <migraphx/gpu/concat.hpp>
#include <migraphx/dom_info.hpp>
#include <migraphx/common_header.hpp>
namespace migraphx {
namespace gpu {

struct find_concur_gpu
{
    void get_concur(program* p,
                    int num_of_streams,
                    std::unordered_map<const instruction*, std::vector<std::vector<const instruction*>>>&
                    concur_instrs)
    {
        dom_info info(p);
        info.compute_dom(true);
        propagate_splits(p, num_of_streams, concur_instrs, info);
    }
    void propagate_splits(program* p, int num_of_streams,
                          std::unordered_map<const instruction*, std::vector<std::vector<const instruction*>>>& concur_instrs, dom_info& info)
    {
        std::unordered_map<instruction_ref, bool> is_split;
        std::unordered_map<instruction_ref, bool> is_merge;
        std::unordered_map<instruction_ref, std::set<const instruction*>> split_from;
        std::unordered_map<const instruction*, int> instr2_points;
        int cur_points = 0;
        instr2_points.clear();

        for(auto ins : iterator_for(*p))
        {
            const instruction* p_iter = &(*ins);
            instr2_points[p_iter]     = cur_points++;
            int stream                = ins->get_stream();
            if(stream < 0)
                continue;

            // Identify split points.
            if(ins->has_mask(RECORD_EVENT))
            {
                std::set<int> stream_set;
                for(auto&& arg : ins->outputs())
                {
                    int arg_stream = arg->get_stream();
                    if(arg_stream >= 0)
                        stream_set.insert(arg_stream);
                }
                if(stream_set.size() > 1)
                    is_split[ins] = true;
            }
            // Identify merge points.
            if(ins->has_mask(WAIT_EVENT))
            {
                std::set<int> stream_set;
                for(auto&& arg : ins->inputs())
                {
                    int arg_stream = arg->get_stream();
                    if(arg_stream >= 0)
                        stream_set.insert(arg_stream);
                }
                if(stream_set.size() > 1)
                    is_merge[ins] = true;
            }

            for(auto&& arg : ins->inputs())
            {
                // Input is a split point.
                if(is_split.find(arg) != is_split.end())
                    split_from[ins].insert(&(*arg));
                // Union inputs' split points.
                if((split_from.find(arg) != split_from.end()) && !split_from[arg].empty())
                {
                    if(split_from.find(ins) == split_from.end())
                        split_from[ins] = split_from[arg];
                    else
                        split_from[ins] = set_op::set_union(split_from[ins], split_from[arg]);
                }
            }

            if(is_merge[ins])
            {
                assert(split_from.find(ins) != split_from.end());
                std::set<const instruction*> del_set;
                // post-dominator kills split point.
                for(auto& split : split_from[ins])
                {
                    if(info.strictly_post_dominates(p_iter, split))
                        del_set.insert(split);
                }
                split_from[ins] = set_op::set_difference(split_from[ins], del_set);
            }

            if(split_from.find(ins) != split_from.end())
            {
                // Collect concur instructions for each split point.
                for(auto& split : split_from[ins])
                {
                    if(concur_instrs.find(split) == concur_instrs.end())
                    {
                        std::vector<std::vector<const instruction*>> instr_stack;
                        instr_stack.resize(num_of_streams);
                        concur_instrs[split] = instr_stack;
                    }
                    concur_instrs[split][stream].push_back(p_iter);
                }
            }
        }
        MIGRAPHX_DEBUG(dump_concur_instrs(concur_instrs));
    }
};

} // namespace gpu

} // namespace migraphx

#endif
