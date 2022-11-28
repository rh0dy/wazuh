#include "cmds/cmdGraph.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

#include <hlp/logpar.hpp>
#include <hlp/registerParsers.hpp>
#include <kvdb/kvdbManager.hpp>
#include <logging/logging.hpp>
#include <rxbk/rxFactory.hpp>
#include <store/drivers/fileDriver.hpp>

#include "base/utils/getExceptionStack.hpp"
#include "builder.hpp"
#include "register.hpp"
#include "registry.hpp"
#include "stackExecutor.hpp"

namespace
{
cmd::StackExecutor g_exitHanlder {};
constexpr auto ENV_GRAPH = "env_graph.dot";
constexpr auto ENV_EXPR_GRAPH = "env_expr_graph.dot";
} // namespace

namespace cmd
{
void graph(const std::string& kvdbPath,
           const std::string& fileStorage,
           const std::string& environment,
           const std::string& graphOutDir)
{
    // Init logging
    // TODO: add cmd to config logging level
    logging::LoggingConfig logConfig;
    logConfig.logLevel = logging::LogLevel::Debug;
    logging::loggingInit(logConfig);
    g_exitHanlder.add([]() { logging::loggingTerm(); });

    auto kvdb = std::make_shared<KVDBManager>(kvdbPath);
    g_exitHanlder.add([kvdb]() { kvdb->clear(); });

    auto store = std::make_shared<store::FileDriver>(fileStorage);
    base::Name hlpConfigFileName({"schema", "wazuh-logpar-types", "0"});
    auto hlpParsers = store->get(hlpConfigFileName);
    if (std::holds_alternative<base::Error>(hlpParsers))
    {
        WAZUH_LOG_ERROR("Could not retreive configuration file [{}] needed by the "
                        "HLP module, error: {}",
                        hlpConfigFileName.fullName(),
                        std::get<base::Error>(hlpParsers).message);

        g_exitHanlder.execute();
        return;
    }
    auto logpar = std::make_shared<hlp::logpar::Logpar>(std::get<json::Json>(hlpParsers));
    hlp::registerParsers(logpar);
    WAZUH_LOG_INFO("HLP initialized");
    auto registry = std::make_shared<builder::internals::Registry>();
    try
    {
        builder::internals::registerBuilders(registry, {kvdb, logpar});
    }
    catch (const std::exception& e)
    {
        WAZUH_LOG_ERROR("Exception while registering builders: [{}]",
                        utils::getExceptionStack(e));
        g_exitHanlder.execute();
        return;
    }

    builder::Builder _builder(store, registry);
    decltype(_builder.buildEnvironment({environment})) env;
    try
    {
        env = _builder.buildEnvironment({environment});
    }
    catch (const std::exception& e)
    {
        WAZUH_LOG_ERROR("Exception while building environment: [{}]",
                        utils::getExceptionStack(e));
        g_exitHanlder.execute();
        return;
    }

    base::Expression envExpression;
    try
    {
        envExpression = env.getExpression();
    }
    catch (const std::exception& e)
    {
        WAZUH_LOG_ERROR("Exception while building environment Expression: [{}]",
                        utils::getExceptionStack(e));
        g_exitHanlder.execute();
        return;
    }

    // Save both graphs
    std::filesystem::path envGraph {graphOutDir};
    envGraph.append(ENV_GRAPH);

    std::filesystem::path envExprGraph {graphOutDir};
    envExprGraph.append(ENV_EXPR_GRAPH);

    std::ofstream graphFile;

    graphFile.open(envGraph.string());
    graphFile << env.getGraphivzStr();
    std::cout << std::endl
              << "Environment graph saved on " << envGraph.string() << std::endl;
    graphFile.close();

    graphFile.open(envExprGraph.string());
    graphFile << base::toGraphvizStr(envExpression);
    std::cout << "Environment expression graph saved on " << envExprGraph.string()
              << std::endl;
    graphFile.close();

    g_exitHanlder.execute();
}
} // namespace cmd
