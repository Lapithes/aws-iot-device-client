// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../IntegrationTestResourceHandler.h"
#include <aws/core/Aws.h>
#include <aws/iot/model/ListThingsInThingGroupRequest.h>
#include <aws/iotsecuretunneling/IoTSecureTunnelingClient.h>
#include <aws/iotsecuretunneling/model/ConnectionStatus.h>
#include <aws/iotsecuretunneling/model/OpenTunnelResult.h>
#include <gtest/gtest.h>
#include <thread>

using namespace Aws;
using namespace Aws::Utils;
using namespace Aws::Auth;
using namespace Aws::Client;
using namespace Aws::IoT;
using namespace Aws::IoT::Model;
using namespace std;

extern string THING_NAME;
extern string PORT;
extern string REGION;
extern bool SKIP_ST;

const string LOCAL_PROXY_PATH = "/localproxy";
const string TEST_TUNNEL_PATH = "/test-tunnel.sh";

class TestSecureTunnelingFixture : public ::testing::Test
{
  public:
    void SetUp() override
    {
        if (!SKIP_ST)
        {

            Aws::InitAPI(options);
            {
                ClientConfiguration clientConfig;
                resourceHandler =
                    unique_ptr<IntegrationTestResourceHandler>(new IntegrationTestResourceHandler(clientConfig));
                Aws::IoTSecureTunneling::Model::OpenTunnelResult openTunnelResult =
                    resourceHandler->OpenTunnel(THING_NAME);
                tunnelId = openTunnelResult.GetTunnelId();
                sourceToken = openTunnelResult.GetSourceAccessToken();

                std::unique_ptr<const char *[]> argv(new const char *[8]);
                argv[0] = LOCAL_PROXY_PATH.c_str();
                argv[1] = "-s";
                argv[2] = PORT.c_str();
                argv[3] = "-r";
                argv[4] = REGION.c_str();
                argv[5] = "-t";
                argv[6] = sourceToken.c_str();
                argv[7] = nullptr;

                PID = fork();
                if (PID == 0)
                {
                    printf("Started Child Process to run Local Proxy\n");
                    if (execvp(LOCAL_PROXY_PATH.c_str(), const_cast<char *const *>(argv.get())) == -1)
                    {
                        printf("Failed to initialize Local Proxy.\n");
                    }
                }
            }
        }
        else
        {
            GTEST_SKIP();
        }
    }
    void TearDown() override
    {
        if (PID == 0)
        {
            _exit(0);
        }
        if (!SKIP_ST)
        {
            resourceHandler->CleanUp();
            Aws::ShutdownAPI(options);
        }
    }
    SDKOptions options;
    unique_ptr<IntegrationTestResourceHandler> resourceHandler;
    string tunnelId;
    string sourceToken;
    int PID;
};

TEST_F(TestSecureTunnelingFixture, SCP)
{
    if (resourceHandler->GetTunnelSourceConnectionStatusWithRetry(tunnelId) !=
        Aws::IoTSecureTunneling::Model::ConnectionStatus::CONNECTED)
    {
        printf("Tunnel Source Failed to connect\n");
        GTEST_FAIL();
    }
    printf("Running %s script...\n", TEST_TUNNEL_PATH.c_str());
    std::unique_ptr<const char *[]> argv(new const char *[3]);
    argv[0] = TEST_TUNNEL_PATH.c_str();
    argv[1] = PORT.c_str();
    argv[2] = nullptr;
    int execResult;
    int pid = fork();
    if (pid == 0)
    {
        if (execvp(TEST_TUNNEL_PATH.c_str(), const_cast<char *const *>(argv.get())) == -1)
        {
            printf("%s failed", TEST_TUNNEL_PATH.c_str());
            _exit(1);
        }
    }
    else
    {
        int waitReturn = waitpid(pid, &execResult, 0);
        if (waitReturn == -1)
        {
            GTEST_FAIL();
        }
    }
    ASSERT_EQ(execResult, 0);
}
