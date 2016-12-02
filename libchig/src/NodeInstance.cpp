#include "chig/NodeInstance.hpp"

using namespace chig;

NodeInstance::NodeInstance(
	std::unique_ptr<NodeType> nodeType, float arg_x, float arg_y, std::string id_)
	: type{std::move(nodeType)}, x{arg_x}, y{arg_y}, id{std::move(id_)}
{
	assert(type);

	inputDataConnections.resize(type->dataInputs.size(), {nullptr, ~0});
	outputDataConnections.resize(type->dataOutputs.size(), {});

	inputExecConnections.resize(type->execInputs.size(), {});
	outputExecConnections.resize(type->execOutputs.size(), {nullptr, ~0});
}

NodeInstance::NodeInstance(const NodeInstance& other)
	: type(other.type->clone()), x{other.x}, y{other.y}, id{other.id + "_"}
{
}

NodeInstance& NodeInstance::operator=(const NodeInstance& other)
{
	type = other.type->clone();
	x = other.x;
	y = other.y;
	id = other.id + "_";

	return *this;
}

namespace chig
{
Result connectData(
	NodeInstance& lhs, size_t connectionInputID, NodeInstance& rhs, size_t connectionOutputID)
{
	Result res;

	// make sure the connection exists
	// the input to the connection is the output to the node
	if (connectionInputID >= lhs.outputDataConnections.size()) {
		auto dataOutputs = nlohmann::json::array();
		for (auto& output : lhs.type->dataOutputs) {
			dataOutputs.push_back(
				{{output.second, lhs.type->context->stringifyType(output.first)}});
		}

		res.add_entry("E22", "Output Data connection doesn't exist in node",
			{{"Requested ID", connectionInputID},
				{"Node Type", lhs.type->module + ":" + lhs.type->name},
				{"Node JSON", rhs.type->toJSON()}, {"Node Output Data Connections", dataOutputs}});
	}
	if (connectionOutputID >= rhs.inputDataConnections.size()) {
		auto dataInputs = nlohmann::json::array();
		for (auto& output : rhs.type->dataInputs) {
			dataInputs.push_back({{output.second, lhs.type->context->stringifyType(output.first)}});
		}

		res.add_entry("E23", "Input Data connection doesn't exist in node",
			{{"Requested ID", connectionOutputID},
				{"Node Type", rhs.type->module + ":" + rhs.type->name},
				{"Node JSON", rhs.type->toJSON()}, {"Node Input Data Connections", dataInputs}});
	}

	// if there are errors, back out
	if (!res) return res;

	// make sure the connection is of the right type
	if (lhs.type->dataOutputs[connectionInputID].first !=
		rhs.type->dataInputs[connectionOutputID].first) {
		res.add_entry("E24", "Connecting data nodes with different types is invalid",
			{{"Left Hand Type",
				 lhs.type->context->stringifyType(lhs.type->dataOutputs[connectionInputID].first)},
				{"Right Hand Type", lhs.type->context->stringifyType(
										rhs.type->dataInputs[connectionOutputID].first)},
				{"Left Node JSON", rhs.type->toJSON()}, {"Right Node JSON", rhs.type->toJSON()}});
		return res;
	}

	// if we are replacing a connection, disconnect it
	if (rhs.inputDataConnections[connectionOutputID].first != nullptr) {
		auto& extconn = rhs.inputDataConnections[connectionOutputID];

		// the node that we were connectd to's vector of output data connections
		auto& extconnvec = extconn.first->outputDataConnections[extconn.second];

		extconnvec.erase(std::find(
			extconnvec.begin(), extconnvec.end(), std::make_pair(&rhs, connectionOutputID)));
	}

	lhs.outputDataConnections[connectionInputID].emplace_back(&rhs, connectionOutputID);
	rhs.inputDataConnections[connectionOutputID] = {&lhs, connectionInputID};

	return res;
}

Result connectExec(
	NodeInstance& lhs, size_t connectionInputID, NodeInstance& rhs, size_t connectionOutputID)
{
	Result res;

	// make sure the connection exists
	if (connectionInputID >= lhs.outputExecConnections.size()) {
		auto execOutputs = nlohmann::json::array();
		for (auto& output : lhs.type->execOutputs) {
			execOutputs.push_back(output);
		}

		res.add_entry("E22", "Output exec connection doesn't exist in node",
			{{"Requested ID", connectionInputID},
				{"Node Type", lhs.type->module + ":" + lhs.type->name},
				{"Node Output Exec Connections", execOutputs}});
	}
	if (connectionOutputID >= rhs.inputExecConnections.size()) {
		auto execInputs = nlohmann::json::array();
		for (auto& output : rhs.type->execInputs) {
			execInputs.push_back(output);
		}

		res.add_entry("E23", "Input exec connection doesn't exist in node",
			{{"Requested ID", connectionInputID},
				{"Node Type", rhs.type->module + ":" + rhs.type->name},
				{"Node Input Exec Connections", execInputs}

			});
	}

	if (!res) return res;

	// if we are replacing a connection, disconnect it
	if (lhs.outputExecConnections[connectionInputID].first != nullptr) {
		auto& extconnvec =
			lhs.outputExecConnections[connectionInputID]
				.first->inputExecConnections[lhs.outputExecConnections[connectionInputID].second];

		extconnvec.erase(std::find(
			extconnvec.begin(), extconnvec.end(), std::make_pair(&lhs, connectionOutputID)));
	}

	// connect it!
	lhs.outputExecConnections[connectionInputID] = {&rhs, connectionOutputID};
	rhs.inputExecConnections[connectionOutputID].emplace_back(&lhs, connectionOutputID);

	return res;
}

Result disconnectData(NodeInstance& lhs, size_t connectionInputID, NodeInstance& rhs)
{
	Result res;

	if (lhs.outputDataConnections.size() >= connectionInputID) {
		auto dataOutputs = nlohmann::json::array();
		for (auto& output : lhs.type->dataOutputs) {
			dataOutputs.push_back(
				{{output.second, lhs.type->context->stringifyType(output.first)}});
		}

		res.add_entry("E22", "Output data connection in node doesn't exist",
			{{"Requested ID", connectionInputID},
				{"Node Type", lhs.type->module + ":" + lhs.type->name},
				{"Node JSON", rhs.type->toJSON()}, {"Node Output Data Connections", dataOutputs}});

		return res;
	}

	// find the connection
	auto iter = std::find_if(lhs.outputDataConnections[connectionInputID].begin(),
		lhs.outputDataConnections[connectionInputID].end(),
		[&](auto& pair) { return pair.first == &rhs; });

	if (iter == lhs.outputDataConnections[connectionInputID].end()) {
		res.add_entry("EUKN", "Cannot disconnect from connection that doesn't exist",
			{{"Left node ID", lhs.id}, {"Right node ID", rhs.id},
				{"Left dock ID", connectionInputID}});

		return res;
	}

	if (rhs.inputDataConnections.size() >= iter->second) {
		auto dataInputs = nlohmann::json::array();
		for (auto& output : rhs.type->dataInputs) {
			dataInputs.push_back({{output.second, lhs.type->context->stringifyType(output.first)}});
		}

		res.add_entry("E23", "Input Data connection doesn't exist in node",
			{{"Requested ID", iter->second}, {"Node Type", rhs.type->module + ":" + rhs.type->name},
				{"Node JSON", rhs.type->toJSON()}, {"Node Input Data Connections", dataInputs}});

		return res;
	}

	if (rhs.inputDataConnections[iter->second] != std::make_pair(&lhs, connectionInputID)) {
		res.add_entry("EUKN", "Cannot disconnect from connection that doesn't exist",
			{{"Left node ID", lhs.id}, {"Right node ID", rhs.id}});

		return res;
	}

	// finally actually disconnect it
	rhs.inputDataConnections[iter->second] = {nullptr, ~0};
	lhs.outputDataConnections[connectionInputID].erase(iter);

	return res;
}

Result disconnectExec(NodeInstance& lhs, size_t connectionInputID)
{
	Result res;

	if (connectionInputID >= lhs.outputExecConnections.size()) {
		auto execOutputs = nlohmann::json::array();
		for (auto& output : lhs.type->execOutputs) {
			execOutputs.push_back(output);
		}

		res.add_entry("E22", "Output exec connection doesn't exist in node",
			{{"Requested ID", connectionInputID},
				{"Node Type", lhs.type->module + ":" + lhs.type->name},
				{"Node Output Exec Connections", execOutputs}});
	}

	auto& lhsconn = lhs.outputExecConnections[connectionInputID];
	auto& rhsconns = lhsconn.first->inputExecConnections[lhsconn.second];

	auto iter = std::find_if(rhsconns.begin(), rhsconns.end(),
		[&](auto pair) { return pair == std::make_pair(&lhs, connectionInputID); });

	if (iter == rhsconns.end()) {
		res.add_entry("EUKN", "Cannot disconnect an exec connection that doesn't connect back",
			{{"Left node ID", lhs.id}, {"Left node dock id", connectionInputID}});
		return res;
	}

	rhsconns.erase(iter);
	lhs.outputExecConnections[connectionInputID] = {nullptr, ~0};

	return res;
}

}  // namespace chig
