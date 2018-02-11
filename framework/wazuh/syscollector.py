#!/usr/bin/env python

# Created by Wazuh, Inc. <info@wazuh.com>.
# This program is a free software; you can redistribute it and/or modify it under the terms of GPLv2

from wazuh import common
from wazuh.exception import WazuhException
from wazuh.agent import Agent
from wazuh import Wazuh
from wazuh.utils import plain_dict_to_nested_dict, cut_array, sort_array, search_array
from operator import itemgetter

def get_os_agent(agent_id, select=None, nested=True):
    """
    Get info about an agent's OS
    """
    # The osinfo fields in database are different in Windows and Linux
    agent_obj = Agent(agent_id)

    os_name = agent_obj.get_agent_attr('os_name')
    windows_fields = {'hostname', 'os_version', 'os_name', 'architecture',
                      'scan_time', 'scan_id'}
    linux_fields   = windows_fields | {'sysname', 'version', 'release'}

    valid_select_fields = windows_fields if 'Windows' in os_name else linux_fields

    if select:
        select_fields = list(set(select['fields']) & set(windows_fields)) \
                        if 'Windows' in os_name else \
                        list(set(select['fields']) & set(linux_fields))
        if select_fields == []:
            uncorrect_fields = map(lambda x: str(x), set(select['fields']) - set(valid_select_fields))
            raise WazuhException(1724, "Allowed select fields: {0}. Fields {1}".\
                format(', '.join(valid_select_fields), ','.join(uncorrect_fields)))
    else:
        select_fields = valid_select_fields

    try:
        info = agent_obj._load_info_from_agent_db(table='sys_osinfo', select=select_fields)[0]
        return plain_dict_to_nested_dict(info) if nested else info
    except IndexError as e:
        # there's no data to return
        return {}


def get_hardware_agent(agent_id, select=None):
    """
    Get info about an agent's OS
    """
    valid_select_fields = ['board_serial', 'cpu_name', 'cpu_cores', 'cpu_mhz',
                           'ram_total', 'ram_free', 'scan_id', 'scan_time']

    if select:
        if not set(select['fields']).issubset(valid_select_fields):
            uncorrect_fields = map(lambda x: str(x), set(select['fields']) - set(valid_select_fields))
            raise WazuhException(1724, "Allowed select fields: {0}. Fields {1}".\
                    format(', '.join(valid_select_fields), ','.join(uncorrect_fields)))
        select_fields = select['fields']
    else:
        select_fields = valid_select_fields

    try:
        return plain_dict_to_nested_dict(Agent(agent_id)._load_info_from_agent_db(table='sys_hwinfo', select=select_fields)[0])
    except IndexError as e:
        return {}


def get_programs_agent(agent_id, offset=0, limit=common.database_limit, select={}, search={}, sort={}, filters={}):
    """
    Get info about an agent's programs
    """
    valid_select_fields = {'scan_id', 'scan_time', 'format', 'name',
                           'vendor', 'version', 'architecture', 'description'}
    allowed_sort_fields = {'scan_id', 'scan_time', 'format', 'name',
                           'vendor', 'version', 'architecture', 'description'}

    if select:
        if not set(select['fields']).issubset(valid_select_fields):
            uncorrect_fields = map(lambda x: str(x), set(select['fields']) - set(valid_select_fields))
            raise WazuhException(1724, "Allowed select fields: {0}. Fields {1}".\
                    format(', '.join(valid_select_fields), ','.join(uncorrect_fields)))
        select_fields = select['fields']
    else:
        select_fields = valid_select_fields

    if search:
        search['fields'] = select_fields

    # Sorting
    if sort and sort['fields']:
        # Check if every element in sort['fields'] is in allowed_sort_fields.
        if not set(sort['fields']).issubset(allowed_sort_fields):
            raise WazuhException(1403, 'Allowed sort fields: {0}. Fields: {1}'.format(allowed_sort_fields, sort['fields']))

    response, total = Agent(agent_id)._load_info_from_agent_db(table='sys_programs',
                                offset=offset, limit=limit, select=select_fields,
                                count=True, sort=sort, search=search, filters=filters)

    return {'totalItems':total, 'items':response}


def get_programs(offset=0, limit=common.database_limit, select=None, sort=None, filters={}, search={}):
    valid_select_fields = {'scan_id', 'scan_time', 'format', 'name',
                           'vendor', 'version', 'architecture', 'description'}
    allowed_sort_fields = {'scan_id', 'scan_time', 'format', 'name',
                           'vendor', 'version', 'architecture', 'description'}

    agents = Agent.get_agents_overview(select={'fields':['id']})['items']
    result = []
    for agent in agents:
        agent_programs = get_programs_agent(agent_id=agent['id'], select=select, filters=filters)
        if agent_programs and len(agent_programs) > 0:
            result.append(agent['id'])

    if search:
        result = search_array(result, search['value'], search['negation'])

    if sort:
        result = sort_array(result, sort['fields'], sort['order'])

    return {'items': cut_array(result, offset, limit), 'totalItems': len(result)}


def get_os(filters={}, offset=0, limit=common.database_limit):
    agents = Agent.get_agents_overview(select={'fields':['id']})['items']

    result = []
    for agent in agents:
        agent_os = get_os_agent(agent_id=agent['id'], select={'fields':['os_name','os_version']}, nested=False)
        passed_filerts = [True for f in filters.keys() if agent_os[f] == filters[f]]
        if not filters or len(passed_filerts) > 0:
            nested_agent_os = plain_dict_to_nested_dict(agent_os)
            current_os = map(itemgetter('os'),result)
            if nested_agent_os['os'] in current_os:
                result[current_os.index(nested_agent_os['os'])]['agent_id'].append(agent['id'])
            else:
                nested_agent_os['agent_id'] = [agent['id']]
                result.append(nested_agent_os)

    return {'items': cut_array(result, offset, limit), 'totalItems': len(result)}


def get_hardware(offset=0, limit=common.database_limit, select=None, sort=None, filters={}, search={}):
    return {} # TO DO