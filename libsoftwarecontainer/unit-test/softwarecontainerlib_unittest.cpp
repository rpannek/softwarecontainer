
/*
 * Copyright (C) 2016 Pelagicore AB
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR
 * BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 *
 * For further information see LICENSE
 */


#include "softwarecontainer_test.h"

#include "gateway/dbusgateway.h"
#include "gateway/waylandgateway.h"
#include "gateway/networkgateway.h"
#include "gateway/pulsegateway.h"
#include "gateway/filegateway.h"
#include "functionjob.h"
#include "commandjob.h"


LOG_DECLARE_DEFAULT_CONTEXT(defaultContext, "ff", "dd");

class SoftwareContainerApp : public SoftwareContainerLibTest
{

public:

    void setGatewayConfigs(const GatewayConfiguration &config)
    {
        getLib().setGatewayConfigs(config);
    }

    Glib::RefPtr<Glib::MainContext> getMainContext()
    {
        return m_context;
    }

    SoftwareContainerLib &getLib()
    {
        return *lib;
    }
};

TEST_F(SoftwareContainerApp, TestWayland) {

    GatewayConfiguration config;
    config[WaylandGateway::ID] = "[ { \"enabled\" : true } ]";

    setGatewayConfigs(config);
    FunctionJob jobTrue(getLib(), [] (){
        bool ERROR = 1;
        bool SUCCESS = 0;

        const char *waylandDir = getenv("XDG_RUNTIME_DIR");
        log_debug() << "Wayland dir : " << waylandDir;
        if (waylandDir == nullptr) {
            return ERROR;
        }

        std::string socketPath = StringBuilder() << waylandDir << "/" << WaylandGateway::SOCKET_FILE_NAME;
        log_debug() << "isSocket : " << socketPath << " " << isSocket(socketPath);

        if ( !isSocket(socketPath) ) {
            return ERROR;
        }

        return SUCCESS;

    });
    jobTrue.start();
    ASSERT_TRUE(jobTrue.wait() == 0);

    CommandJob westonJob(getLib(),"/bin/sh -c \"/usr/bin/weston-info > /dev/null\"");
    westonJob.start();
    ASSERT_TRUE(westonJob.wait() == 0);

}


/*
 * Tests the convenience functions in common.cpp
 */
TEST_F(SoftwareContainerApp, CommonFunctions) {

    ASSERT_TRUE(isDirectory("/tmp"));

    // Create a temp file and check so it is a file
    char tempFilename[] = "/tmp/blablaXXXXXX";
    int fileDescriptor = mkstemp(tempFilename);
    close(fileDescriptor);
    ASSERT_TRUE(isFile(tempFilename));
    ASSERT_FALSE(isDirectory(tempFilename));
    ASSERT_FALSE(isSocket(tempFilename));

    // Let's use the same file and test the read/write functions
    std::string testData = "testData";
    std::string readBack;
    writeToFile(tempFilename, testData);
    readFromFile(tempFilename, readBack);
    ASSERT_EQ(testData, readBack);

    // And remove the file
    ASSERT_TRUE(unlink(tempFilename) == 0);

    // New temp file
    fileDescriptor = mkstemp(tempFilename);
    close(fileDescriptor);

    // Create a socket
    int sock = socket (PF_LOCAL, SOCK_DGRAM, 0);
    struct sockaddr_un addr;
    strcpy(addr.sun_path, tempFilename);
    addr.sun_family = AF_UNIX;
    bind(sock, (struct sockaddr *) &addr, strlen(addr.sun_path) + sizeof (addr.sun_family));

    ASSERT_TRUE(isSocket(tempFilename));
    ASSERT_FALSE(isFile(tempFilename));
    ASSERT_FALSE(isDirectory(tempFilename));

    const char *unexistingFile = "/run/user/10jhgj00/X11-dgfdgdagisplay";
    ASSERT_FALSE(isSocket(unexistingFile));
    ASSERT_FALSE(isFile(unexistingFile));
    ASSERT_FALSE(isDirectory(unexistingFile));

    // Reading from a nonexixsting file should fail
    ReturnCode c1 = readFromFile(unexistingFile, readBack);
    ASSERT_EQ(c1, ReturnCode::FAILURE);

    ReturnCode c2 = writeToFile(unexistingFile, testData);
    ASSERT_EQ(c2, ReturnCode::FAILURE);
}

static constexpr int EXISTENT = 1;
static constexpr int NON_EXISTENT = 0;

TEST_F(SoftwareContainerApp, FileGatewayReadOnly) {

    // Make sure /tmp exists in both host and container
    ASSERT_TRUE(isDirectory("/tmp"));
    FunctionJob job0(getLib(), [&] () {
        return isDirectory("/tmp") ? EXISTENT : NON_EXISTENT;
    });
    job0.start();
    ASSERT_TRUE(job0.wait() == EXISTENT);

    // Create two temporary files
    char tempFilename1[] = "/tmp/fileGatewayXXXXXX";
    char tempFilename2[] = "/tmp/fileGatewayXXXXXX";
    int fd1 = mkstemp(tempFilename1);
    int fd2 = mkstemp(tempFilename2);

    // We could indeed create them.
    ASSERT_TRUE(fd1 != 0);
    ASSERT_TRUE(fd2 != 0);

    close(fd1);
    close(fd2);

    // They will be mapped to these files
    std::string containerPath1 = "testFile1";
    std::string containerPath2 = "testFile2";

    // tempFilename2 will be symlinked into the container, so it will be
    // available at the same path inside as outside. Make sure it's not
    // there before we've mounted it.
    FunctionJob jobNotMounted(getLib(), [&] () {
        return isFile(tempFilename2) ? EXISTENT : NON_EXISTENT;
    });
    jobNotMounted.start();
    ASSERT_TRUE(jobNotMounted.wait() == NON_EXISTENT);

    // We configure the FileGateway to have both files mounted to their
    // respective locations. The first one's location can be found through
    // an environment variable, the other will have the same name inside
    // the container as outside.
    std::string envVarName = "TEST_FILE_PATH";
    GatewayConfiguration config;
    std::string configStr =
    "["
        "{"
            "  \"path-host\" : \"" + std::string(tempFilename1) + "\""
            ", \"path-container\" : \"" + containerPath1 + "\""
            ", \"create-symlink\" : false"
            ", \"read-only\": true"
            ", \"env-var-name\": \"" + envVarName + "\""
            ", \"env-var-prefix\": \"\""
            ", \"env-var-suffix\": \"\""
        "},"
        "{"
            "  \"path-host\" : \"" + std::string(tempFilename2) + "\""
            ", \"path-container\" : \"" + containerPath2 + "\""
            ", \"create-symlink\" : true"
            ", \"read-only\": true"
        "}"
    "]";
    config[FileGateway::ID] = configStr;
    setGatewayConfigs(config);

    // Make sure the environment variables are available
    FunctionJob jobEnv(getLib(), [&] () {
        return getenv(envVarName.c_str()) != nullptr ? EXISTENT : NON_EXISTENT;
    });
    jobEnv.start();
    ASSERT_TRUE(jobEnv.wait() == EXISTENT);

    // Now the files pointed out by the env var should be available
    FunctionJob jobEnvFile(getLib(), [&] () {
        std::string envVal = getenv(envVarName.c_str());
        return isFile(envVal) ? EXISTENT : NON_EXISTENT;
    });
    jobEnvFile.start();
    ASSERT_TRUE(jobEnvFile.wait() == EXISTENT);

    // The files with symlinks should be available at its original location,
    // but inside the container
    FunctionJob jobSymlink(getLib(), [&] () {
        return isFile(tempFilename2) ? EXISTENT : NON_EXISTENT;
    });
    jobSymlink.start();
    ASSERT_TRUE(jobSymlink.wait() == EXISTENT);

    // Write some data to the files outside the container and make sure we can
    // read it inside the container
    std::string testData = "testdata";
    writeToFile(tempFilename1, testData);
    writeToFile(tempFilename2, testData);

    // Test if we can read the data back into a variable
    FunctionJob jobReadData(getLib(), [&] () {
        std::string envVal = getenv(envVarName.c_str());
        std::string readBack;
        readFromFile(envVal, readBack);
        return readBack == testData ? 0 : 1;
    });
    jobReadData.start();
    ASSERT_TRUE(jobReadData.wait() == 0);

    // Make sure we can't write to the file
    std::string badData = "This data should never be read";
    FunctionJob jobWriteDataRO(getLib(), [&] () {
        std::string envVal = getenv(envVarName.c_str());
        writeToFile(envVal, badData);
        return 0;
    });
    jobWriteDataRO.start();
    jobWriteDataRO.wait();

    std::string readBack = "";
    readFromFile(tempFilename1, readBack);
    ASSERT_EQ(testData, readBack);

    // Remove the temp files
    ASSERT_TRUE(unlink(tempFilename1) == 0);
    ASSERT_TRUE(unlink(tempFilename2) == 0);
}

TEST_F(SoftwareContainerApp, FileGatewayReadWrite) {
    // Make sure /tmp exists in both host and container
    ASSERT_TRUE(isDirectory("/tmp"));
    FunctionJob job0(getLib(), [&] () {
        return isDirectory("/tmp") ? EXISTENT : NON_EXISTENT;
    });
    job0.start();
    ASSERT_TRUE(job0.wait() == EXISTENT);

    // Create two temporary files
    char tempFilename1[] = "/tmp/fileGatewayXXXXXX";
    char tempFilename2[] = "/tmp/fileGatewayXXXXXX";
    int fd1 = mkstemp(tempFilename1);
    int fd2 = mkstemp(tempFilename2);

    // We could indeed create them.
    ASSERT_TRUE(fd1 != 0);
    ASSERT_TRUE(fd2 != 0);

    close(fd1);
    close(fd2);

    // They will be mapped to these files
    std::string containerPath1 = "testFile1";
    std::string containerPath2 = "testFile2";

    // tempFilename2 will be symlinked into the container, so it will be
    // available at the same path inside as outside. Make sure it's not
    // there before we've mounted it.
    FunctionJob jobNotMounted(getLib(), [&] () {
        return isFile(tempFilename2) ? EXISTENT : NON_EXISTENT;
    });
    jobNotMounted.start();
    ASSERT_TRUE(jobNotMounted.wait() == NON_EXISTENT);

    std::string envVarName = "TEST_FILE_PATH";
    GatewayConfiguration config;
    std::string configStr =
    "["
        "{" // The files below are mounted read-only
            "  \"path-host\" : \"" + std::string(tempFilename1) + "\""
            ", \"path-container\" : \"" + containerPath1 + "\""
            ", \"create-symlink\" : false"
            ", \"read-only\": false"
            ", \"env-var-name\": \"" + envVarName + "\""
            ", \"env-var-prefix\": \"\""
            ", \"env-var-suffix\": \"\""
        "},"
        "{"
            "  \"path-host\" : \"" + std::string(tempFilename2) + "\""
            ", \"path-container\" : \"" + containerPath2 + "\""
            ", \"create-symlink\" : true"
            ", \"read-only\": false"
        "}"
    "]";
    config[FileGateway::ID] = configStr;
    setGatewayConfigs(config);

    // Make sure the environment variables are available
    FunctionJob jobEnv(getLib(), [&] () {
        return getenv(envVarName.c_str()) != nullptr ? EXISTENT : NON_EXISTENT;
    });
    jobEnv.start();
    ASSERT_TRUE(jobEnv.wait() == EXISTENT);

    // Now the files pointed out by the env var should be available
    FunctionJob jobEnvFile(getLib(), [&] () {
        std::string envVal = getenv(envVarName.c_str());
        return isFile(envVal) ? EXISTENT : NON_EXISTENT;
    });
    jobEnvFile.start();
    ASSERT_TRUE(jobEnvFile.wait() == EXISTENT);

    // The files with symlinks should be available at its original location,
    // but inside the container
    FunctionJob jobSymlink(getLib(), [&] () {
        return isFile(tempFilename2) ? EXISTENT : NON_EXISTENT;
    });
    jobSymlink.start();
    ASSERT_TRUE(jobSymlink.wait() == EXISTENT);

    // Write some data to the files outside the container and make sure we can
    // read it inside the container
    std::string testData = "testdata";
    writeToFile(tempFilename1, testData);
    writeToFile(tempFilename2, testData);

    // Test if we can read the data back into a variable
    FunctionJob jobReadData(getLib(), [&] () {
        std::string envVal = getenv(envVarName.c_str());
        std::string readBack;
        readFromFile(envVal, readBack);
        return readBack == testData ? 0 : 1;
    });
    jobReadData.start();
    ASSERT_TRUE(jobReadData.wait() == 0);

    // Make sure we can write to the file
    std::string newData = "This data should have been written";
    FunctionJob jobWriteData(getLib(), [&] () {
        std::string envVal = getenv(envVarName.c_str());
        writeToFile(envVal, newData);
        return 0;
    });
    jobWriteData.start();
    jobWriteData.wait();

    std::string readBack = "";
    readFromFile(tempFilename1, readBack);
    ASSERT_EQ(newData, readBack);

    // Let's remove the temp files also
    ASSERT_TRUE(unlink(tempFilename1) == 0);
    ASSERT_TRUE(unlink(tempFilename2) == 0);
}

/**
 * Test whether the mounting of files works properly
 */
TEST_F(SoftwareContainerApp, TestFileMounting) {

    char tempFilename[] = "/tmp/blablaXXXXXX";
    int fileDescriptor = mkstemp(tempFilename);

    ASSERT_TRUE(fileDescriptor != 0);

    // create a temporary file with some content
    const char *content = "GFDGDFHDHRWG";
    write(fileDescriptor, content, sizeof(content));
    close(fileDescriptor);

    FunctionJob job1(getLib(), [&] () {
                return isFile(tempFilename) ? EXISTENT : NON_EXISTENT;
            });
    job1.start();
    ASSERT_TRUE(job1.wait() == NON_EXISTENT);

    std::string pathInContainer;
    ReturnCode result = getLib().getContainer()->bindMountFileInContainer(tempFilename, basename(strdup(tempFilename)), pathInContainer, true);
    ASSERT_TRUE(isSuccess(result));

    FunctionJob job2(getLib(), [&] () {
                return isFile(pathInContainer) ? EXISTENT : NON_EXISTENT;
            });
    job2.start();
    ASSERT_TRUE(job2.wait() == EXISTENT);

}


/**
 * Test whether the mounting of folder works properly
 */
TEST_F(SoftwareContainerApp, TestFolderMounting) {

    char tempDirname[] = "/tmp/blablaXXXXXX";
    mkdtemp(tempDirname);
    ASSERT_TRUE(isDirectory(tempDirname));

    FunctionJob job1(getLib(), [&] () {
                return isDirectory(tempDirname) ? EXISTENT : NON_EXISTENT;
            });
    job1.start();
    ASSERT_TRUE(job1.wait() == NON_EXISTENT);

    std::string pathInContainer;
    ReturnCode result = getLib().getContainer()->bindMountFolderInContainer(tempDirname, basename(strdup(tempDirname)), pathInContainer, true);
    ASSERT_TRUE(isSuccess(result));

    FunctionJob job2(getLib(), [&] () {
                return isDirectory(pathInContainer) ? EXISTENT : NON_EXISTENT;
            });
    job2.start();
    ASSERT_TRUE(job2.wait() == EXISTENT);

    // Write some data to a file inside the directory
    char *tempFilename = strcat(tempDirname, "/bluhuXXXXXX");
    int fileDescriptor = mkstemp(tempFilename);
    const char *content = "GFDGDFHDHRWG";
    write(fileDescriptor, content, sizeof(content));
    close(fileDescriptor);
    ASSERT_TRUE(isFile(tempFilename));

    FunctionJob job3(getLib(), [&] () {
                return isFile(pathInContainer + "/" + basename(strdup(tempFilename))) ? EXISTENT : NON_EXISTENT;
            });
    job3.start();
    ASSERT_TRUE(job3.wait() == EXISTENT);
}

#include <stdlib.h>

/**
 * Test whether the mounting of sockets works properly
 */
TEST_F(SoftwareContainerApp, TestUnixSocket) {

    char tempFilename[] = "/tmp/blablaXXXXXX";
    char *tempDirname = mkdtemp(tempFilename);

    ASSERT_TRUE(isDirectory(tempDirname));

    std::string pathInContainer;
    ReturnCode result = getLib().getContainer()->bindMountFolderInContainer(tempDirname, basename(strdup(tempDirname)), pathInContainer, true);
    ASSERT_TRUE(isSuccess(result));

    char *tmp = new char[pathInContainer.size() + 8];
    std::copy(pathInContainer.begin(), pathInContainer.end(), tmp);
    tmp[pathInContainer.size()] = '\0';
    char *tempUnixSocket = strcat(tmp, "/socket");


    FunctionJob job1(getLib(), [&] () {

                int fd, fd2, done, n;
                char str[100];
                socklen_t t;
                sockaddr_un local, remote;
                local.sun_family = AF_UNIX;
                strcpy(local.sun_path, tempUnixSocket);
                fd = socket(AF_UNIX, SOCK_STREAM, 0);
                bind(fd, (sockaddr*)(&local), sizeof(local));
                listen(fd, 100);

                t = sizeof(remote);
                if ((fd2 = accept(fd, (struct sockaddr *)&remote, &t)) == -1) {
                    return 0;
                }

                done = 0;
                do {
                    n = recv(fd2, str, 100, 0);
                    if (n <= 0) {
                        if (n < 0) perror("recv");
                        done = 1;
                    }

                } while (!done);

                close(fd2);

                return done == 1 ? EXISTENT : NON_EXISTENT;
            });


    pid_t forkpid = fork();

    if(forkpid >= 0) // fork was successful
    {
        if(forkpid == 0) // child process
        {
            int s, len;
            struct sockaddr_un remote;
            char str[] = "TestData";

            sleep(1);

            if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
                _exit(NON_EXISTENT);
            }

            char *x = new char[strlen(tempDirname) + 8];
            strcpy(x, tempDirname);
            strcat(x, "/socket");

            remote.sun_family = AF_UNIX;
            strcpy(remote.sun_path, x);
            len = strlen(remote.sun_path) + sizeof(remote.sun_family);
            if (connect(s, (struct sockaddr *)&remote, len) == -1) {
                _exit(NON_EXISTENT);
            }


            if (send(s, str, strlen(str), 0) == -1) {
                _exit(NON_EXISTENT);
            }

            close(s);
            _exit(EXISTENT);
        }
        else //Parent process
        {
            job1.start();
            ASSERT_TRUE(job1.wait() == EXISTENT);
        }
    }
    else // fork failed
    {
        printf("\n Fork failed, quitting!!!!!!\n");
        return;
    }

}


TEST(SoftwareContainerLib, MultithreadTest) {

    static const int TIMEOUT = 20;

    SoftwareContainerLib lib;
    lib.setMainLoopContext(Glib::MainContext::get_default());

    bool finished = false;

    auto f = [&]() {
        log_info() << "Initializing";
        lib.init();
        finished = true;
    };

    std::thread t(f);

    for (int i = 0; (i < TIMEOUT) && !finished; i++) {
        log_info() << "Waiting for softwarecontainer to be initialized";
        sleep(1);
    }

    ASSERT_TRUE(finished);

    if (finished) {
        t.join();
    }

}

TEST_F(SoftwareContainerApp, TestPulseAudioEnabled) {

    GatewayConfiguration config;
    config[PulseGateway::ID] = "[ { \"audio\" : true } ]";
    setGatewayConfigs(config);

    // We need access to the test file, so we bind mount it
    std::string soundFileCPP = std::string(TEST_DATA_DIR) + std::string("/Rear_Center.wav");
    const char *soundFile = soundFileCPP.c_str();

    std::string pathInContainer;
    ReturnCode result = getLib().getContainer()->bindMountFileInContainer(soundFile, basename(strdup(soundFile)), pathInContainer, true);
    ASSERT_TRUE(isSuccess(result));

    // Make sure the file is there
    FunctionJob job1(getLib(), [&] () {
        return isFile(pathInContainer) ? EXISTENT : NON_EXISTENT;
    });
    job1.start();
    ASSERT_TRUE(job1.wait() == EXISTENT);

    CommandJob job2(getLib(), "/usr/bin/paplay " + pathInContainer);
    job2.start();
    ASSERT_TRUE(job2.isRunning());
    ASSERT_TRUE(job2.wait() == 0);
}


TEST_F(SoftwareContainerApp, TestStdin) {
    CommandJob job(getLib(), "/bin/cat");
    job.captureStdin();
    job.captureStdout();
    job.start();
    ASSERT_TRUE(job.isRunning());

    const char outputBytes[] = "test string";
    char inputBytes[sizeof(outputBytes)] = {};

    unsigned int writtenBytesCount = write(job.stdin(), outputBytes, sizeof(outputBytes));
    ASSERT_EQ(writtenBytesCount, sizeof(outputBytes));

    unsigned int readBytesCount = read(job.stdout(), inputBytes, sizeof(inputBytes));
    ASSERT_EQ(readBytesCount, sizeof(outputBytes));

    SignalConnectionsHandler connections;
    addProcessListener(connections, job.pid(), [&] (
                int pid, int exitCode) {
                log_debug() << "finished process :" << job.toString();
                exit();
            }, getMainContext());

    kill(job.pid(), SIGTERM);

    run();
}


/**
 * We do not enable the network gateway so we expect the ping to fail
 */
TEST_F(SoftwareContainerApp, TestNetworkInternetCapabilityDisabled) {
    CommandJob job(getLib(), "/bin/sh -c \"ping www.google.com -c 5 -q > /dev/null\"");
    job.start();
    ASSERT_TRUE(job.wait() != 0);

    CommandJob job2(getLib(), "/bin/sh -c \"ping 8.8.8.8 -c 5 -q > /dev/null\"");
    job2.start();
    ASSERT_TRUE(job2.wait() != 0);
}

/**
 * This test checks that an external is accessible after the network gateway has been enabled to access the internet.
 */
TEST_F(SoftwareContainerApp, TestNetworkInternetCapabilityEnabled) {

    GatewayConfiguration config;
    config[NetworkGateway::ID] = "[ { \"internet-access\" : true, \"gateway\" : \"10.0.3.1\" } ]";
    setGatewayConfigs(config);

    CommandJob job2(getLib(), "/bin/sh -c \"ping 8.8.8.8 -c 5 -q > /dev/null\"");
    job2.start();
    ASSERT_TRUE(job2.wait() == 0);

    CommandJob job(getLib(), "/bin/sh -c \"ping www.google.com -c 5 -q > /dev/null\"");
    job.start();
    ASSERT_TRUE(job.wait() == 0);
}


//TEST(SoftwareContainerLib, TestStartUnexistingApp) {
//	SoftwareContainerApp app;
//	SoftwareContainerLib& lib = app.lib;
//	Job job(lib, "/usr/bin/notexisting");
//	job.start();
//	// TODO : implement
//}


TEST_F(SoftwareContainerApp, TestJobReturnCode) {

    CommandJob jobTrue(getLib(), "/bin/true");
    jobTrue.start();
    ASSERT_TRUE(jobTrue.wait() == 0);

    CommandJob jobFalse(getLib(), "/bin/false");
    jobFalse.start();
    ASSERT_FALSE(jobFalse.wait() == 0);
}

/**
 * Checks that DBUS daemons are accessible if the corresponding capability is enabled
 */
TEST_F(SoftwareContainerApp, TestDBusGatewayWithAccess) {
    GatewayConfiguration config;
    config[DBusGateway::ID] = "[{"
        "\"dbus-gateway-config-session\": [{ \"direction\": \"*\", \"interface\": \"*\", \"object-path\": \"*\", \"method\": \"*\" }], "
        "\"dbus-gateway-config-system\": [{ \"direction\": \"*\", \"interface\": \"*\", \"object-path\": \"*\", \"method\": \"*\" }]"
    "}]";

    log_error() << config[DBusGateway::ID];
    setGatewayConfigs(config);

    {
        CommandJob jobTrue(
                getLib(),
                "/usr/bin/dbus-send --system --dest=org.freedesktop.DBus / org.freedesktop.DBus.Introspectable.Introspect");
        jobTrue.start();
        ASSERT_TRUE(jobTrue.wait() == 0);
    }

    {
        CommandJob jobTrue(
                getLib(),
                "/usr/bin/dbus-send --session --dest=org.freedesktop.DBus / org.freedesktop.DBus.Introspectable.Introspect");
        jobTrue.start();
        ASSERT_TRUE(jobTrue.wait() == 0);
    }
}

// Regression test against previously reported bug.
TEST_F(SoftwareContainerApp, TestDBusGatewayOutputBuffer) {
    GatewayConfiguration config;
    config[DBusGateway::ID] = "[{"
        "\"dbus-gateway-config-session\": [{ \"direction\": \"*\", \"interface\": \"*\", \"object-path\": \"*\", \"method\": \"*\" }], "
        "\"dbus-gateway-config-system\": [{ \"direction\": \"*\", \"interface\": \"*\", \"object-path\": \"*\", \"method\": \"*\" }]"
    "}]";

    log_error() << config[DBusGateway::ID];
    setGatewayConfigs(config);

    for(int i=0; i<2000; i++) {
        CommandJob jobTrue(
                getLib(),
                "/usr/bin/dbus-send --session --dest=org.freedesktop.DBus / org.freedesktop.DBus.Introspectable.Introspect");
        jobTrue.start();
        ASSERT_TRUE(jobTrue.wait() == 0);
    }
}

/**
 * Checks that DBUS is not accessible if the corresponding capability is not enabled
 */
TEST_F(SoftwareContainerApp, TestDBusGatewayWithoutAccess) {

    {
        CommandJob jobTrue(
                getLib(),
                "/usr/bin/dbus-send --session --print-reply --dest=org.freedesktop.DBus / org.freedesktop.DBus.Introspectable.Introspect");
        jobTrue.start();

        ASSERT_TRUE(jobTrue.wait() != 0);
    }

    {
        CommandJob jobTrue(
                getLib(),
                "/usr/bin/dbus-send --system --print-reply --dest=org.freedesktop.DBus / org.freedesktop.DBus.Introspectable.Introspect");
        jobTrue.start();

        // We expect the system bus to be accessible, even if we can not access any service. TODO : test if the services are accessible
        ASSERT_TRUE(jobTrue.wait() != 0);
    }

}


TEST_F(SoftwareContainerApp, InitTest) {
    ASSERT_TRUE(getLib().isInitialized());
}
