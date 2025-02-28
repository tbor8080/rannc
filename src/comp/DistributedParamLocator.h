//
// Created by Masahiro Tanaka on 2021/05/13.
//

#ifndef PYRANNC_DISTRIBUTEDPARAMLOCATOR_H
#define PYRANNC_DISTRIBUTEDPARAMLOCATOR_H

#include <torch/torch.h>
#include <comm/NCCLWrapper.h>

#include "graph/ir.h"
#include "DistributedParamLocatorBase.h"

namespace rannc {

    class DistributedParamLocator : public DistributedParamLocatorBase {
    public:
        DistributedParamLocator(const DistributedParamLocator&) = delete;
        DistributedParamLocator& operator=(const DistributedParamLocator&) = delete;
        DistributedParamLocator(DistributedParamLocator&&) = delete;
        DistributedParamLocator& operator=(DistributedParamLocator&&) = delete;

        void store(long pid, const at::Tensor& param);
        at::Tensor load(long pid);
        void remove(long pid);

        void fetchStart();
        at::Tensor fetch(long pid);
        void fetchEnd();

        static DistributedParamLocator& get() {
            static DistributedParamLocator instance;
            return instance;
        }

    private:
        DistributedParamLocator() = default;

        std::unordered_map<long, at::Tensor> param_parts_;
    };
}

#endif //PYRANNC_DISTRIBUTEDPARAMLOCATOR_H
