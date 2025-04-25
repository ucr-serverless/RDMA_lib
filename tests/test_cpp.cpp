#include <gtest/gtest.h>
#include <iostream>
#include "RDMA_cpp.h"

TEST(RDMA_base, OpenDevice)
{
    auto ptr = rdma_find_dev("mlx5_0");
    EXPECT_TRUE(ptr);
    auto ptr2 = rdma_find_dev("mlx5");
    EXPECT_TRUE(ptr2 == nullptr);


}
