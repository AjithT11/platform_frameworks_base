/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.server.pm;


import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.parsing.AndroidPackage;
import android.content.pm.parsing.ComponentParseUtils.ParsedActivity;
import android.content.pm.parsing.ComponentParseUtils.ParsedActivityIntentInfo;
import android.content.pm.parsing.PackageImpl;
import android.content.pm.parsing.ParsingPackage;
import android.os.Build;
import android.os.Process;
import android.permission.IPermissionManager;
import android.util.ArrayMap;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;

import java.util.Map;

@RunWith(JUnit4.class)
public class AppsFilterTest {

    private static final int DUMMY_CALLING_UID = 10345;

    @Mock
    IPermissionManager mPermissionManagerMock;

    @Mock
    AppsFilter.FeatureConfig mFeatureConfigMock;

    private Map<String, AndroidPackage> mExisting = new ArrayMap<>();

    private static ParsingPackage pkg(String packageName) {
        return PackageImpl.forParsing(packageName)
                .setTargetSdkVersion(Build.VERSION_CODES.R);
    }

    private static ParsingPackage pkg(String packageName, Intent... queries) {
        ParsingPackage pkg = pkg(packageName);
        if (queries != null) {
            for (Intent intent : queries) {
                pkg.addQueriesIntent(intent);
            }
        }
        return pkg;
    }

    private static ParsingPackage pkg(String packageName, String... queriesPackages) {
        ParsingPackage pkg = pkg(packageName);
        if (queriesPackages != null) {
            for (String queryPackageName : queriesPackages) {
                pkg.addQueriesPackage(queryPackageName);
            }
        }
        return pkg;
    }

    private static ParsingPackage pkg(String packageName, IntentFilter... filters) {
        ParsedActivity activity = new ParsedActivity();
        activity.setPackageName(packageName);
        for (IntentFilter filter : filters) {
            final ParsedActivityIntentInfo info = new ParsedActivityIntentInfo(packageName, null);
            if (filter.countActions() > 0) {
                filter.actionsIterator().forEachRemaining(info::addAction);
            }
            if (filter.countCategories() > 0) {
                filter.actionsIterator().forEachRemaining(info::addAction);
            }
            if (filter.countDataAuthorities() > 0) {
                filter.authoritiesIterator().forEachRemaining(info::addDataAuthority);
            }
            if (filter.countDataSchemes() > 0) {
                filter.schemesIterator().forEachRemaining(info::addDataScheme);
            }
            activity.addIntent(info);
        }

        return pkg(packageName)
                .addActivity(activity);
    }

    @Before
    public void setup() throws Exception {
        mExisting = new ArrayMap<>();

        MockitoAnnotations.initMocks(this);
        when(mPermissionManagerMock
                .checkPermission(anyString(), anyString(), anyInt()))
                .thenReturn(PackageManager.PERMISSION_DENIED);
        when(mFeatureConfigMock.isGloballyEnabled()).thenReturn(true);
        when(mFeatureConfigMock.packageIsEnabled(any(AndroidPackage.class)))
                .thenReturn(true);
    }

    @Test
    public void testSystemReadyPropogates() throws Exception {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock, new String[]{}, false);
        appsFilter.onSystemReady();
        verify(mFeatureConfigMock).onSystemReady();
    }

    @Test
    public void testQueriesAction_FilterMatches() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock, new String[]{}, false);

        PackageSetting target = simulateAddPackage(appsFilter,
                pkg("com.some.package", new IntentFilter("TEST_ACTION"))).build();
        PackageSetting calling = simulateAddPackage(appsFilter,
                pkg("com.some.other.package", new Intent("TEST_ACTION"))).build();

        assertFalse(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, calling, target, 0));
    }

    @Test
    public void testQueriesAction_NoMatchingAction_Filters() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock, new String[]{}, false);

        PackageSetting target = simulateAddPackage(appsFilter,
                pkg("com.some.package")).build();
        PackageSetting calling = simulateAddPackage(appsFilter,
                pkg("com.some.other.package", new Intent("TEST_ACTION"))).build();

        assertTrue(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, calling, target, 0));
    }

    @Test
    public void testQueriesAction_NoMatchingActionFilterLowSdk_DoesntFilter() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock,
                        new String[]{}, false);

        PackageSetting target = simulateAddPackage(appsFilter, pkg("com.some.package")).build();
        PackageSetting calling = simulateAddPackage(appsFilter, pkg("com.some.other.package",
                new Intent("TEST_ACTION")).setTargetSdkVersion(
                Build.VERSION_CODES.P)).build();

        assertFalse(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, calling, target, 0));
    }

    @Test
    public void testNoQueries_Filters() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock,
                        new String[]{}, false);

        PackageSetting target = simulateAddPackage(appsFilter, pkg("com.some.package")).build();
        PackageSetting calling = simulateAddPackage(appsFilter,
                pkg("com.some.other.package")).build();

        assertTrue(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, calling, target, 0));
    }

    @Test
    public void testForceQueryable_DoesntFilter() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock,
                        new String[]{}, false);

        PackageSetting target =
                simulateAddPackage(appsFilter, pkg("com.some.package").setForceQueryable(true))
                        .build();
        PackageSetting calling = simulateAddPackage(appsFilter,
                pkg("com.some.other.package")).build();

        assertFalse(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, calling, target, 0));
    }

    @Test
    public void testForceQueryableByDevice_SystemCaller_DoesntFilter() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock,
                        new String[]{"com.some.package"}, false);

        PackageSetting target = simulateAddPackage(appsFilter, pkg("com.some.package"))
                .setPkgFlags(ApplicationInfo.FLAG_SYSTEM)
                .build();
        PackageSetting calling = simulateAddPackage(appsFilter,
                pkg("com.some.other.package")).build();

        assertFalse(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, calling, target, 0));
    }

    @Test
    public void testForceQueryableByDevice_NonSystemCaller_Filters() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock,
                        new String[]{"com.some.package"}, false);

        PackageSetting target = simulateAddPackage(appsFilter, pkg("com.some.package")).build();
        PackageSetting calling = simulateAddPackage(appsFilter,
                pkg("com.some.other.package")).build();

        assertTrue(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, calling, target, 0));
    }


    @Test
    public void testSystemQueryable_DoesntFilter() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock,
                        new String[]{}, true /* system force queryable */);

        PackageSetting target = simulateAddPackage(appsFilter, pkg("com.some.package"))
                .setPkgFlags(ApplicationInfo.FLAG_SYSTEM)
                .build();
        PackageSetting calling = simulateAddPackage(appsFilter,
                pkg("com.some.other.package")).build();

        assertFalse(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, calling, target, 0));
    }

    @Test
    public void testQueriesPackage_DoesntFilter() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock,
                        new String[]{}, false);

        PackageSetting target = simulateAddPackage(appsFilter, pkg("com.some.package")).build();
        PackageSetting calling = simulateAddPackage(appsFilter,
                pkg("com.some.other.package", "com.some.package")).build();

        assertFalse(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, calling, target, 0));
    }

    @Test
    public void testNoQueries_FeatureOff_DoesntFilter() {
        when(mFeatureConfigMock.packageIsEnabled(any(AndroidPackage.class)))
                .thenReturn(false);
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock,
                        new String[]{}, false);

        PackageSetting target = simulateAddPackage(appsFilter, pkg("com.some.package")).build();
        PackageSetting calling = simulateAddPackage(appsFilter,
                pkg("com.some.other.package")).build();

        assertFalse(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, calling, target, 0));
    }

    @Test
    public void testSystemUid_DoesntFilter() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock,
                        new String[]{}, false);

        PackageSetting target = simulateAddPackage(appsFilter, pkg("com.some.package")).build();

        assertFalse(appsFilter.shouldFilterApplication(0, null, target, 0));
        assertFalse(appsFilter.shouldFilterApplication(
                Process.FIRST_APPLICATION_UID - 1, null, target, 0));
    }

    @Test
    public void testNonSystemUid_NoCallingSetting_Filters() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock,
                        new String[]{}, false);

        PackageSetting target = simulateAddPackage(appsFilter, pkg("com.some.package")).build();

        assertTrue(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, null, target, 0));
    }

    @Test
    public void testNoTargetPackage_filters() {
        final AppsFilter appsFilter =
                new AppsFilter(mFeatureConfigMock, mPermissionManagerMock,
                        new String[]{}, false);

        PackageSetting target = new PackageSettingBuilder()
                .setName("com.some.package")
                .setCodePath("/")
                .setResourcePath("/")
                .setPVersionCode(1L)
                .build();
        PackageSetting calling = simulateAddPackage(appsFilter,
                pkg("com.some.other.package", new Intent("TEST_ACTION"))).build();

        assertTrue(appsFilter.shouldFilterApplication(DUMMY_CALLING_UID, calling, target, 0));
    }

    private PackageSettingBuilder simulateAddPackage(AppsFilter filter,
            ParsingPackage newPkgBuilder) {
        AndroidPackage newPkg = newPkgBuilder
                .hideAsParsed()
                .hideAsFinal();
        filter.addPackage(newPkg, mExisting);
        mExisting.put(newPkg.getPackageName(), newPkg);
        return new PackageSettingBuilder()
                .setPackage(newPkg)
                .setName(newPkg.getPackageName())
                .setCodePath("/")
                .setResourcePath("/")
                .setPVersionCode(1L);
    }

}

