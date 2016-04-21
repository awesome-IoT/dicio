import alt from '../alt';
import {API_GROUPS_URL} from '../lib/Constants';

class GroupActions {
  groupsChanged(groups) {
    return groups;
  }

  groupChanged(group) {
    return group;
  }

  fetchGroups() {
  	fetch(API_GROUPS_URL)
      .then((response) => response.json())
      .then((responseData) => {
        this.groupsChanged(responseData);
      })
      .catch(console.error)
      .done();
    return [];
  }

  fetchGroup(group_id) {
    fetch(`${API_GROUPS_URL}/${group_id}`)
      .then((response) => response.json())
      .then((responseData) => {
        this.groupChanged(responseData);
      })
      .catch(console.error)
      .done();
    return {};
  }

  updateGroup(group_id, updatedParams) {
    console.log("updating: ", updatedParams);
    fetch(`${API_GROUPS_URL}/${group_id}`, {
      method: 'POST',
      headers: {
        'Accept': 'application/json',
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(updatedParams)
    })
      .then((response) => response.json())
      .then((responseData) => {
        this.groupChanged(responseData);
      })
      .catch(console.error)
      .done();
    return {group_id, updatedParams};
  }

}

export default alt.createActions(GroupActions);
