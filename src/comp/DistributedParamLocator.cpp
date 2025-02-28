//
// Created by Masahiro Tanaka on 2021/05/13.
//

#include "DistributedParamLocator.h"

#include "comm/ObjectComm.h"
#include "comm/MPIUtil.h"
#include "comm/NCCLWrapper.h"
#include "comm/SComm.h"


namespace rannc {

    void DistributedParamLocator::store(long pid, const at::Tensor& param) {
        const auto ranks = mpi::getAllRanks();
        doRegister(pid, param, ranks);

        assert(offsets_.at(pid).size() == ranks.size());
        assert(src_sizes_.at(pid).size() == ranks.size());

        int local_rank = getLocalRank(ranks, mpi::getRank());
        int64_t offset = offsets_.at(pid).at(local_rank);
        int64_t src_size = src_sizes_.at(pid).at(local_rank);
        int64_t segment_size = segment_sizes_.at(pid);

        at::TensorOptions options;
        options = options.dtype(param.dtype()).device(param.device()).requires_grad(param.requires_grad());
        at::Tensor part_tensor = torch::zeros({segment_size}, options);

        if (src_size > 0) {
            torch::NoGradGuard no_grad;
            auto src_buf = torch::flatten(param).slice(0, offset, offset + src_size);
            auto dst_buf = torch::flatten(part_tensor).slice(0, 0, src_size);
            dst_buf.copy_(src_buf);
        }
        param_parts_[pid] = part_tensor;
    }

    at::Tensor DistributedParamLocator::load(long pid) {
        assert(contains(segment_sizes_, pid));
        assert(contains(ranks_, pid));
        assert(contains(ir_types_, pid));

        assert(contains(param_parts_, pid));
        auto param_part = param_parts_.at(pid);

        const IRType& ir_type = ir_types_.at(pid);
        at::TensorOptions options;
        options = options.dtype(fromIRTensorElemTypeToScalarType(ir_type.getTensorElemType()))
                .requires_grad(param_part.requires_grad())
                .device(c10::Device(c10::DeviceType::CUDA));
        at::Tensor buf = torch::zeros({(int64_t)(segment_sizes_.at(pid)*ranks_.at(pid).size())}, options);

        TagMap& tag_map = TagMap::get();
        int tag = tag_map.getRankSetTag(mpi::getAllRanks());
        nccl_.createCommunicator(tag, mpi::getAllRanks());

        at::NoGradGuard no_grad;

        param_part.set_requires_grad(false);
        const auto sendbuf = param_part.cuda();

        nccl_.allgather(tag, {sendbuf}, {buf});
        param_part.set_requires_grad(true);

        return buf.slice(0, 0, productDim(ir_type.getTensorDim()))
                .view(ir_type.getTensorDim())
                .cpu().detach();
    }

    void DistributedParamLocator::fetchStart() {
        TagMap& tag_map = TagMap::get();
        comm_tag_ = tag_map.getRankSetTag(mpi::getAllRanks());
        nccl_.createCommunicator(comm_tag_, mpi::getAllRanks());

        if (mpi::getRank() != 0) {
            long global_pid;
            MPI_Bcast(&global_pid, 1, MPI_LONG, 0, MPI_COMM_WORLD);

            while (global_pid != 0) {
                assert(contains(global_id_to_local_, global_pid));
                load(global_id_to_local_.at(global_pid));
                MPI_Bcast(&global_pid, 1, MPI_LONG, 0, MPI_COMM_WORLD);
            }
        }
    }

    at::Tensor DistributedParamLocator::fetch(long pid) {
        MPI_Bcast(&pid, 1, MPI_LONG, 0, MPI_COMM_WORLD);
        return load(pid);
    }

    void DistributedParamLocator::fetchEnd() {
        if (mpi::getRank() == 0) {
            long pid = 0;
            MPI_Bcast(&pid, 1, MPI_LONG, 0, MPI_COMM_WORLD);
        }
    }

    void DistributedParamLocator::remove(long pid) {
        DistributedParamLocatorBase::remove(pid);
        param_parts_.erase(pid);
    }
}