import { Box, Tab, TabList, TabPanel, TabPanels, Tabs } from "@chakra-ui/react";
import React from "react";
import { HashRouter, Route, Switch } from "react-router-dom";
import { ChakraCustomProvider } from "../../theme/ChakraCustomProvider";
import { useDisableNavBar } from "../../utils/useDisableNavBar";
import { useGlobalStyleReset } from "../../utils/useGlobalStyleReset";
import { QueryEditorPane } from "./editor/QueryEditorPane";
import { QueryContextProvider, useQueryContext } from "./QueryContextProvider";
import { QueryFullGraphView } from "./queryGraph/QueryFullGraphView";
import { RunningQueries } from "./runningQueries/RunningQueries";
import { SlowQueryHistory } from "./slowQueries/SlowQueryHistory";

export const QueryViewWrap = () => {
  useDisableNavBar();
  useGlobalStyleReset();
  return (
    <ChakraCustomProvider overrideNonReact>
      <QueryContextProvider>
        <HashRouter basename="/" hashType={"noslash"}>
          <Switch>
            <Route path="/queries">
              <QueryViewWrapInner />
            </Route>
          </Switch>
        </HashRouter>
      </QueryContextProvider>
    </ChakraCustomProvider>
  );
};

const QueryViewWrapInner = () => {
  const { queryGraphResult } = useQueryContext();
  if (queryGraphResult && queryGraphResult.result) {
    return <QueryFullGraphView />;
  }
  return (
    <Box width="full" height="calc(100vh - 60px)" overflow="auto">
      <Tabs height="full" isLazy>
        <TabList>
          <Tab>Editor</Tab>
          <Tab>Running Queries</Tab>
          <Tab>Slow Query History</Tab>
        </TabList>
        <TabPanels height="calc(100% - 60px)">
          <TabPanel height="full">
            <QueryEditorPane />
          </TabPanel>
          <TabPanel height="full">
            <RunningQueries />
          </TabPanel>
          <TabPanel height="full">
            <SlowQueryHistory />
          </TabPanel>
        </TabPanels>
      </Tabs>
    </Box>
  );
};
